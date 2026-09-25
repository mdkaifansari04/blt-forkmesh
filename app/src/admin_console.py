"""On-demand implementation of the production administrator console.

Cloudflare validates a Python Worker by compiling and executing its entrypoint
under the isolate memory limit. The console's sizeable HTML/CSS renderer and
generic D1 table tooling are irrelevant to normal public requests, so entry.py
loads this module only when an administrator route needs one of its exports.
"""


def _bind_runtime(runtime):
    """Supply the entrypoint primitives used by the console implementation."""
    namespace = globals()
    for name, value in runtime.items():
        if not name.startswith("__") and name not in namespace:
            namespace[name] = value


ADMIN_STYLE = """
 /* Palette for the markup the section renderers still emit: tables, forms,
    the record detail list and the install diagnostics grid. Follows the
    theme class admin_shell stamps (html.theme-light / html.theme-dark, with
    the OS preference as the fallback), so the two stylesheets agree.
    Every rule is scoped under .ab-root, which wraps a section's content and
    nothing else — the shell's own chrome sits outside it and is styled only
    by admin_shell.STYLE. */
 :root{
   --ab-bg:#0d1117;--ab-fg:#c9d1d9;--ab-muted:#8b949e;--ab-border:#21262d;
   --ab-border-2:#30363d;--ab-card:#161b22;--ab-link:#58a6ff;
   --ab-danger:#f85149;--ab-btn:#238636;--ab-btn-hover:#2ea043;
   --ab-btn-fg:#ffffff;--ab-ok-bg:#11251a;--ab-ok-fg:#aff5c2}
 html.theme-light{
   --ab-bg:#ffffff;--ab-fg:#1f2328;--ab-muted:#59636e;--ab-border:#d1d9e0;
   --ab-border-2:#d1d9e0;--ab-card:#f6f8fa;--ab-link:#0969da;
   --ab-danger:#cf222e;--ab-btn:#1f883d;--ab-btn-hover:#1a7f37;
   --ab-btn-fg:#ffffff;--ab-ok-bg:#dafbe1;--ab-ok-fg:#116329}
 .ab-root .layout-single{padding:0}
 @media (prefers-color-scheme:light){html:not(.theme-dark){
   --ab-bg:#ffffff;--ab-fg:#1f2328;--ab-muted:#59636e;--ab-border:#d1d9e0;
   --ab-border-2:#d1d9e0;--ab-card:#f6f8fa;--ab-link:#0969da;
   --ab-danger:#cf222e;--ab-btn:#1f883d;--ab-btn-hover:#1a7f37;
   --ab-btn-fg:#ffffff;--ab-ok-bg:#dafbe1;--ab-ok-fg:#116329}}
 .ab-root{color:var(--ab-fg)}
 .ab-root .meta{color:var(--ab-muted);font-size:13px;margin-top:4px}
 .ab-root a{color:var(--ab-link);text-decoration:none}
 .ab-root a:hover{text-decoration:underline}
 .ab-root .cards{display:flex;gap:12px;flex-wrap:wrap;padding:12px 24px 0}
 .ab-root .card{background:var(--ab-card);border:1px solid var(--ab-border);border-radius:8px;padding:10px 16px;min-width:110px}
 .ab-root .card .n{font-size:22px;font-weight:600}
 .ab-root .card .l{color:var(--ab-muted);font-size:12px;margin-top:2px}
 .ab-root .card.warn .n{color:var(--ab-danger)}
 .ab-root .tools{padding:10px 24px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
 .ab-root button{background:var(--ab-btn);color:var(--ab-btn-fg);border:1px solid var(--ab-btn-hover);border-radius:6px;
        padding:6px 12px;font:600 13px system-ui;cursor:pointer}
 .ab-root button:hover{background:var(--ab-btn-hover)}
 .ab-root button[disabled]{background:var(--ab-border-2);border-color:var(--ab-border-2);color:var(--ab-muted);cursor:not-allowed}
 .ab-root input[type=text],.ab-root input[type=password],.ab-root select{background:var(--ab-bg);color:var(--ab-fg);
        border:1px solid var(--ab-border-2);border-radius:6px;padding:6px 8px;font:13px system-ui}
 .ab-root .banner{margin:0 24px 8px;padding:10px 14px;border-radius:6px;border:1px solid var(--ab-btn-hover);
         background:var(--ab-ok-bg);color:var(--ab-ok-fg);white-space:pre-wrap;font:13px ui-monospace,monospace}
 .ab-root .admin-setting{margin:12px 24px 4px;padding:14px 16px;border:1px solid var(--ab-border-2);
        border-radius:8px;background:var(--ab-card);display:flex;align-items:center;justify-content:space-between;
        gap:18px;scroll-margin-top:20px;outline:none}
 .ab-root .admin-setting:focus{box-shadow:0 0 0 3px color-mix(in srgb,var(--ab-link) 35%,transparent)}
 .ab-root .admin-setting h2{font-size:15px;margin:0 0 4px}
 .ab-root .admin-setting p{color:var(--ab-muted);font-size:12px;margin:0;max-width:760px}
 .ab-root .admin-setting form{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
 .ab-root .admin-setting label{white-space:nowrap;font-size:13px}
 .ab-root .layout{display:flex;align-items:flex-start;gap:16px;width:100%}
 .ab-root nav{width:210px;flex:none;border:1px solid var(--ab-border);
        border-radius:8px;background:var(--ab-card);padding:8px 0}
 .ab-root nav a{display:flex;align-items:baseline;justify-content:space-between;
        gap:8px;padding:5px 16px;color:var(--ab-fg);font-size:13px}
 .ab-root nav a.active{background:var(--ab-bg);box-shadow:inset 3px 0 0 var(--ab-link);font-weight:600}
 .ab-root nav .sec{padding:10px 16px 4px;color:var(--ab-muted);font-size:11px;text-transform:uppercase;letter-spacing:.04em}
 .ab-root nav .navsort{float:right;text-transform:none;letter-spacing:normal;font-size:11px}
 .ab-root main{flex:1;min-width:0;max-width:none;margin:0;overflow-x:auto;padding:0}
 .ab-root table{border-collapse:collapse;width:100%}
 .ab-root th,.ab-root td{text-align:left;padding:5px 10px;border-bottom:1px solid var(--ab-border);vertical-align:top}
 .ab-root th{position:sticky;top:0;background:var(--ab-card);color:var(--ab-muted);font-weight:600;
        font-size:12px;white-space:nowrap;z-index:1}
 .ab-root th.jcol{font-style:italic;font-weight:500}
 .ab-root td{font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap;word-break:break-word;max-width:560px}
 .ab-root table.compact td{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
        word-break:normal;max-width:240px}
 .ab-root table.compact td[title]{cursor:help;text-decoration:underline dotted var(--ab-muted);
        text-underline-offset:3px}
 .ab-root .jsoncell{color:var(--ab-muted)}
 .ab-root .s5{color:var(--ab-danger);font-weight:600}
 .ab-root tbody tr:hover{background:var(--ab-card)}
 .ab-root tr.record-row{cursor:pointer}
 .ab-root tr.record-row:focus{outline:2px solid var(--ab-link);outline-offset:-2px;background:var(--ab-card)}
 .ab-root .empty{padding:32px 24px;color:var(--ab-muted)}
 .ab-root .title{padding:12px 24px 4px;font-weight:600}
 .ab-root .navcount{color:var(--ab-muted);font-size:11px;font-weight:400}
 .ab-root .navlink{color:var(--ab-link)}
 .ab-root nav a .navcount{flex:none}
 .ab-root .rowform{padding:8px 24px;max-width:760px}
 .ab-root .rowfield{display:block;margin:10px 0}
 .ab-root .rowfield span{display:block;color:var(--ab-muted);font-size:12px;margin-bottom:4px}
 .ab-root .rowfield input,.ab-root .rowfield textarea{width:100%;background:var(--ab-bg);color:var(--ab-fg);
        border:1px solid var(--ab-border-2);border-radius:6px;padding:8px;
        font:13px ui-monospace,monospace}
 .ab-root .record-detail{margin:12px 24px;max-width:980px;border:1px solid var(--ab-border-2);
        border-radius:8px;background:var(--ab-card);overflow:hidden}
 .ab-root .record-detail dl{margin:0}
 .ab-root .record-detail dl>div{display:block;padding:12px 16px;border-bottom:1px solid var(--ab-border)}
 .ab-root .record-detail dl>div:last-child{border-bottom:0}
 .ab-root .record-detail dt{color:var(--ab-muted);font:600 12px system-ui,sans-serif;margin-bottom:5px}
 .ab-root .record-detail dd{margin:0;color:var(--ab-fg);white-space:pre-wrap;overflow-wrap:anywhere;
        font:13px/1.55 ui-monospace,monospace}
 .ab-root .tools .navlink{padding:8px 4px}
 .ab-root .account-kind{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
 .ab-root .account-kind button{padding:3px 8px;font-size:12px}
 /* The migration cell holds pills + buttons that must all stay visible, so it
    opts out of the compact table's single-line clip/ellipsis + 240px cap. */
 .ab-root table.compact td.account-cell{max-width:none;overflow:visible;white-space:normal}
 .ab-root .kindpill{border:1px solid var(--ab-border-2);border-radius:999px;padding:2px 8px;
        color:var(--ab-fg);background:var(--ab-card);font:600 12px system-ui,sans-serif}
 .ab-root .inpill{border:1px solid #1a7f37;border-radius:999px;padding:1px 7px;
        color:#1a7f37;background:transparent;font:600 11px system-ui,sans-serif;white-space:nowrap}
 .ab-root .diaggrid{display:flex;gap:24px;flex-wrap:wrap;padding:4px 24px 12px;align-items:flex-start}
 .ab-root .diagcol{min-width:240px}
 .ab-root .diagcol h3{font-size:13px;color:var(--ab-muted);margin:8px 0 4px;font-weight:600}
 .ab-root .diagcol table{width:auto;min-width:220px}
 .ab-root .error-analytics{padding:4px 24px 18px}
 .ab-root .error-chart{height:150px;display:grid;grid-template-columns:repeat(24,minmax(8px,1fr));gap:4px;align-items:end;border-bottom:1px solid var(--ab-border);padding-top:12px}
 .ab-root .error-bar{min-height:2px;background:var(--ab-link);border-radius:3px 3px 0 0;position:relative}
 .ab-root .error-bar[data-empty="true"]{background:var(--ab-border)}
 .ab-root .error-bar:focus{outline:2px solid var(--ab-fg);outline-offset:2px}
 .ab-root .error-hours{display:flex;justify-content:space-between;color:var(--ab-muted);font-size:11px;margin-top:6px}
 .ab-root .error-groups{margin-top:16px}
 .ab-root .error-sparkline{width:180px;height:34px;display:grid;grid-template-columns:repeat(24,1fr);
        gap:2px;align-items:end;border-bottom:1px solid var(--ab-border);padding:2px 0}
 .ab-root .error-sparkline:focus{outline:2px solid var(--ab-link);outline-offset:2px}
 .ab-root .error-spark-bar{display:block;min-height:2px;background:var(--ab-link);
        border-radius:2px 2px 0 0}
 .ab-root .error-spark-bar[data-empty="true"]{background:var(--ab-border)}
 .ab-root .error-source{display:inline-block;border:1px solid var(--ab-border-2);
        border-radius:999px;padding:1px 7px;white-space:nowrap;
        font:600 11px system-ui,sans-serif}
 .ab-root .error-source.javascript{border-color:#8957e5;color:#a371f7}
 .ab-root .error-source.worker{border-color:var(--ab-link);color:var(--ab-link)}
 .ab-root .error-source.desktop{border-color:#2ea043;color:#3fb950}
 .ab-root .error-delete{background:transparent;color:var(--ab-danger);
        border-color:var(--ab-danger);padding:3px 8px;font-size:11px}
 .ab-root .error-delete:hover{background:var(--ab-danger);color:#fff}
 .ab-root .error-group-delete{display:inline;margin:0}
 .ab-root .error-bot{background:transparent;color:var(--ab-link);
        border-color:var(--ab-link);padding:3px 8px;font-size:11px;white-space:nowrap}
 .ab-root .error-bot:hover{background:var(--ab-link);color:#fff}
 .ab-root .error-bot-task{display:inline;margin:0}
 .ab-root .error-message{display:inline-block;max-width:640px;
        white-space:pre-wrap;overflow-wrap:anywhere;vertical-align:middle}
 .ab-root .error-copy{background:transparent;color:var(--ab-muted);
        border-color:var(--ab-border-2);padding:1px 6px;font-size:11px;
        margin-left:6px;vertical-align:middle}
 .ab-root .error-copy:hover{color:var(--ab-fg);border-color:var(--ab-fg)}
 .ab-root .error-users{color:var(--ab-fg);white-space:nowrap}
 .ab-root .error-anonymous{color:var(--ab-muted)}
 .ab-root .relative-time{color:var(--ab-muted);white-space:nowrap}
"""

# Cloudflare D1 internal bookkeeping stays out of the browser. The following
# application tables remain in the inventory but their rows are restricted:
# platform administration is not a decryption capability and the generic UI is
# not a least-privilege security-review interface.
ADMIN_HIDDEN_TABLES = (
    "_cf_KV",
    "accounts",
    "repositories",
    "repo_shares",
    "mirror_requests",
    "repo_agents",
    "agent_prompts",
    "owner_encryption_keys",
    "repo_privacy_policy",
    "repo_terms_flags",
    "security_reports",
    "repo_security_scans",
    "repo_security_scan_reviews",
    "role_grants",
    "sensitive_audit_log",
    # Succession is governed only by the organization's current roster. The
    # generic platform-admin table editor must not create approvals, alter
    # cases, or bypass the append-only history/role-transfer guard.
    "org_succession_policies",
    "org_succession_cases",
    "org_succession_approvals",
    "org_succession_events",
    "chain_intents",
    "pending_rewards",
    # Legacy tables can contain Worker-held wallet seeds. They remain frozen
    # for an offline migration, but must never be rendered or edited over HTTP.
    "central_fund",
    "bounty_wallet",
    "issue_bounty",
    "federated_signup",
    # Relay identity is not a Solana wallet, but its private signing seed is
    # likewise outside the generic platform-admin decryption boundary.
    "relay_self",
    # Identity, authorization, mirror-routing and reward state are available
    # only through their purpose-built, audited APIs. A platform administrator
    # is not implicitly a repository owner, mirror signer, or reward signer.
    "account_devices",
    "account_presence",
    "account_ssh_keys",
    "nodes",
    "org_members",
    "org_repos",
    "org_team_members",
    "org_teams",
    "orgs",
    "mirror_https_endpoints",
    "private_mirror_routes",
    "reward_contribution_intents",
    "reward_contributions",
    "reward_node_observations",
    "reward_rounds",
    "funds_received",
    "clone_rr",
    "edge_route_cursor",
)

# The admin navigation is a live inventory of every application table. Tables
# that hold credentials, encrypted owner data, or audit evidence stay listed so
# an operator can account for the whole schema, but _render_table_view keeps
# their contents restricted.
#
# Row selection + bulk delete (the same "tick rows, Delete selected" UI as the
# error log) is available for every table whose rows are actually rendered —
# i.e. everything not in ADMIN_HIDDEN_TABLES. Sensitive/identity/custody
# tables stay fully restricted above, so there is no separate purge allowlist
# to keep in sync as tables are added.
def _admin_purge_allowed(table):
    return table not in ADMIN_HIDDEN_TABLES


async def _admin_list_tables(env):
    rows = await d1_all(
        env,
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%' AND name NOT LIKE '_cf_%' ORDER BY name",
    )
    return [
        str(r.get("name", ""))
        for r in rows
        if (
            r.get("name")
        )
    ]


# Generic-table cells render on a single compact line (CSS ellipsizes long
# values); anything longer than the preview rides in a hover tooltip so rows
# stay one line tall.
ADMIN_CELL_PREVIEW = 120
ADMIN_CELL_TOOLTIP_MAX = 4000
# Cap on extra columns expanded from the decrypted `data` JSON blob.
ADMIN_MAX_JSON_COLS = 24


_ADMIN_WALLET_KEY_NAMES = frozenset({
    "donationsecret",
    "walletsecret",
    "walletprivatekey",
    "solanasecret",
    "solanaprivatekey",
    "seedbase64url",
    "mnemonic",
    "recoveryphrase",
})


def _admin_contains_wallet_key(value):
    if isinstance(value, dict):
        normalized_keys = {
            re.sub(r"[^a-z0-9]", "", str(key).lower())
            for key in value
        }
        for key, item in value.items():
            normalized = re.sub(r"[^a-z0-9]", "", str(key).lower())
            if normalized in _ADMIN_WALLET_KEY_NAMES:
                return True
            # Historical bounty rows used the generic pair {address, secret}.
            if normalized == "secret" and "address" in normalized_keys:
                return True
            if _admin_contains_wallet_key(item):
                return True
    elif isinstance(value, list):
        return any(_admin_contains_wallet_key(item) for item in value)
    return False


def _admin_redact_wallet_keys(value):
    if isinstance(value, dict):
        normalized_keys = {
            re.sub(r"[^a-z0-9]", "", str(key).lower())
            for key in value
        }
        redacted = {}
        for key, item in value.items():
            normalized = re.sub(r"[^a-z0-9]", "", str(key).lower())
            if (
                normalized in _ADMIN_WALLET_KEY_NAMES
                or (normalized == "secret" and "address" in normalized_keys)
            ):
                redacted[key] = "<restricted: offline custody migration>"
            else:
                redacted[key] = _admin_redact_wallet_keys(item)
        return redacted
    if isinstance(value, list):
        return [_admin_redact_wallet_keys(item) for item in value]
    return value


def _admin_compact_cell(value, extra_class=""):
    if value is None or value == "":
        return "<td></td>"
    if isinstance(value, bool):
        text = "true" if value else "false"
    elif isinstance(value, (dict, list)):
        text = json.dumps(value, sort_keys=True)
    else:
        text = str(value)
    if len(text) > ADMIN_CELL_TOOLTIP_MAX:
        text = text[:ADMIN_CELL_TOOLTIP_MAX] + "…"
    shown = text
    if len(shown) > ADMIN_CELL_PREVIEW:
        shown = shown[:ADMIN_CELL_PREVIEW] + "…"
    title = (' title="%s"' % _html_escape(text)) if shown != text else ""
    cls = (' class="%s"' % extra_class) if extra_class else ""
    return "<td%s%s>%s</td>" % (cls, title, _html_escape(shown))


def _admin_json_cell(decoded):
    # Collapse the decrypted `data` JSON to a small marker; the full pretty
    # JSON shows in the hover tooltip.
    decoded = _admin_redact_wallet_keys(decoded)
    pretty = json.dumps(decoded, indent=2, sort_keys=True)
    if len(pretty) > ADMIN_CELL_TOOLTIP_MAX:
        pretty = pretty[:ADMIN_CELL_TOOLTIP_MAX] + "…"
    return ('<td class="jsoncell" title="%s">{…} %d key(s)</td>'
            % (_html_escape(pretty), len(decoded)))


# --- Admin bulk select + delete helpers (operate on rowid) -------------------

def _admin_bulk_form_open(table, csrf_field="", admin_query=""):
    # Per-row buttons ride this form with their own formaction (forms cannot
    # nest) and run their own confirmation, so the bulk prompt must not also
    # fire for them — it asked "delete the selected rows?" for a row action
    # that deletes nothing.
    return (
        '<form method="post" action="%s" '
        'onsubmit="return (event.submitter&&'
        "event.submitter.hasAttribute('formaction'))||"
        'confirm(\'Delete the selected '
        'row(s)? This cannot be undone.\')">'
        '%s'
        '<div class="tools">'
        '<button type="submit">Delete selected</button>'
        '<span class="meta">Tick rows (or the header box for all) then delete.'
        '</span></div>'
    ) % (_admin_href(admin_query, table=table, action="delete_rows"), csrf_field)


def _admin_select_all_th():
    return (
        '<th><input type="checkbox" title="Select all" '
        "onclick=\"for(const c of this.closest('table')"
        ".querySelectorAll('input[name=ids]'))c.checked=this.checked\"></th>"
    )


def _admin_row_checkbox(rowid):
    return ('<td><input type="checkbox" name="ids" value="%s"></td>'
            % _html_escape(rowid))


def _admin_error_source(method, path):
    """Stable display classification for the shared operational error log."""
    # Reports posted by the Qt desktop app (POST /api/desktop-errors) share the
    # log with browser and Worker errors: a third source, not a Worker fault.
    if (
        str(method or "").strip().upper() == "APP"
        or str(path or "").startswith("/desktop-error/")
    ):
        return "Desktop"
    is_javascript = (
        str(method or "").strip().upper() == "JS"
        or str(path or "").startswith("/client-error/")
    )
    return "JavaScript" if is_javascript else "Worker"


def _admin_error_source_badge(method, path):
    source = _admin_error_source(method, path)
    return (
        '<span class="error-source %s">%s</span>'
        % (source.lower(), source)
    )


def _admin_error_related_users(actors, anonymous=0):
    """Summarize which accounts an error (or error group) actually hit.

    `actors` maps account name -> occurrence count. Anonymous occurrences are
    counted separately rather than dropped: "3 signed-in users plus anonymous
    traffic" and "only anonymous traffic" are very different bugs.
    """
    ranked = sorted(
        (actors or {}).items(), key=lambda item: (-item[1], item[0]))
    parts = ["%s (%d)" % (name, hits) for name, hits in ranked]
    if int(anonymous or 0):
        parts.append("anonymous (%d)" % int(anonymous))
    if not parts:
        return "—"
    return ", ".join(parts)


def _admin_error_users_cell(actors, anonymous=0):
    summary = _admin_error_related_users(actors, anonymous)
    shown = summary if len(summary) <= 60 else summary[:60] + "…"
    return (
        '<td class="error-users" title="%s">%s</td>'
        % (_html_escape(summary), _html_escape(shown))
    )


def _admin_error_row_user_cell(actor):
    name = str(actor or "").strip().lower()
    return (
        '<td class="error-users">%s</td>'
        % (_html_escape(name) if name
           else '<span class="error-anonymous">anonymous</span>')
    )


def _admin_error_copy_button(text):
    """Copy the full (untruncated) error text the cell only previews."""
    return (
        '<button type="button" class="error-copy" data-copy="%s" '
        'title="Copy this message" '
        'onclick="event.stopPropagation()">Copy</button>'
        % _html_escape(text or "")
    )


def _admin_error_bot_task_fields(status, method, path, message, users=""):
    return "".join(
        '<input type="hidden" name="%s" value="%s">'
        % (_html_escape(name), _html_escape(value))
        for name, value in (
            ("group_status", status),
            ("group_method", method),
            ("group_path", path),
            ("group_message", message),
            ("group_users", users),
        )
    )


def _admin_error_bot_task_form(
        status, method, path, message, users="",
        csrf_field="", admin_query=""):
    """Send one error (or equivalent-error group) to the organization tasks."""
    return (
        '<form class="error-bot-task" method="post" action="%s" '
        '>%s%s<button class="error-bot" type="submit">'
        "Send to task</button></form>"
        % (
            _admin_href(
                admin_query, table="error_log", action="create_bot_task"),
            csrf_field,
            _admin_error_bot_task_fields(
                status, method, path, message, users),
        )
    )


def _admin_error_row_delete_button(rowid, admin_query=""):
    return (
        '<button class="error-delete" type="submit" name="error_id" '
        'value="%s" formaction="%s" '
        'onclick="event.stopPropagation();return confirm('
        "'Delete this error? This cannot be undone.')\">Delete</button>"
        % (
            _html_escape(rowid),
            _admin_href(
                admin_query, table="error_log", action="delete_error_row"),
        )
    )


def _admin_error_row_bot_task_button(rowid, admin_query=""):
    # The raw table's rows already live inside the bulk-delete form, so this
    # rides that form with its own formaction (forms cannot nest). Only the
    # row id travels; the server re-reads the stored error itself.
    return (
        '<button class="error-bot" type="submit" name="error_id" '
        'value="%s" formaction="%s" '
        'onclick="event.stopPropagation()">Send to task</button>'
        % (
            _html_escape(rowid),
            _admin_href(
                admin_query, table="error_log", action="create_bot_task"),
        )
    )


def _admin_error_group_delete_form(
        status, method, path, message, csrf_field="", admin_query=""):
    fields = "".join(
        '<input type="hidden" name="%s" value="%s">'
        % (_html_escape(name), _html_escape(value))
        for name, value in (
            ("group_status", status),
            ("group_method", method),
            ("group_path", path),
            ("group_message", message),
        )
    )
    return (
        '<form class="error-group-delete" method="post" action="%s" '
        'onsubmit="return confirm('
        "'Delete every error in this group? This cannot be undone.')\">"
        "%s%s<button class=\"error-delete\" type=\"submit\">"
        "Delete group</button></form>"
        % (
            _admin_href(
                admin_query, table="error_log",
                action="delete_error_group"),
            csrf_field,
            fields,
        )
    )


def _admin_record_row_attrs(admin_query, table, rowid):
    href = _admin_href(
        admin_query, table=table, action="detail", rowid=rowid)
    return (
        ' class="record-row" tabindex="0" role="link" data-href="%s" '
        'onclick="if(!event.target.closest(\'input,button,a,label\'))'
        'location.href=this.dataset.href" '
        'onkeydown="if(event.key===\'Enter\')location.href=this.dataset.href"'
        % _html_escape(href)
    )


async def _admin_table_columns(env, table):
    # Real column names for the table (table name is validated by the caller).
    rows = await d1_all(env, "PRAGMA table_info(" + table + ")")
    return [str(r.get("name", "")) for r in rows if r.get("name")]


async def _render_row_form(env, table, rowid, csrf_field="", admin_query=""):
    # Retained as a fail-closed target for old admin edit/new bookmarks.
    # Generic create/update was an ambient authorization bypass: a platform
    # admin could edit users, org membership, SSH keys, mirror proofs or reward
    # state without satisfying the domain API's signature and policy checks.
    # Keep this helper as a fail-closed response for stale bookmarks.
    return (
        '<div class="empty">Generic row editing is disabled. Use the '
        "purpose-built, audited administration action for this resource.</div>"
    )


async def _render_record_detail(env, table, rowid, admin_query=""):
    if table in ADMIN_HIDDEN_TABLES:
        return '<div class="empty">This table is restricted.</div>'
    try:
        rowid = int(rowid)
    except (TypeError, ValueError):
        return '<div class="empty">Record not found.</div>'
    if rowid <= 0:
        return '<div class="empty">Record not found.</div>'
    row = await d1_first(
        env,
        "SELECT rowid AS _rowid_, * FROM " + table + " WHERE rowid=? LIMIT 1",
        rowid,
    )
    if not row:
        return '<div class="empty">Record not found.</div>'
    fields = [("_rowid_", rowid)]
    for key, value in row.items():
        if key == "_rowid_":
            continue
        if key == "data" and isinstance(value, str) and value:
            decoded = await decrypt_row(env, value)
            value = (
                _admin_redact_wallet_keys(decoded)
                if isinstance(decoded, (dict, list))
                else decoded
            )
        if isinstance(value, (dict, list)):
            value = json.dumps(value, indent=2, sort_keys=True, default=str)
        elif value is None:
            value = "NULL"
        fields.append((key, value))
    rows = "".join(
        "<div><dt>%s</dt><dd>%s</dd></div>"
        % (_html_escape(key), _html_escape(value))
        for key, value in fields
    )
    return (
        '<div class="title">Record detail · %s · row %d</div>'
        '<div class="tools"><a class="navlink" href="%s">← Back to %s</a>'
        '<span class="meta">Read-only vertical view. Sensitive tables remain '
        'restricted and encrypted data is redacted before display.</span></div>'
        '<section class="record-detail"><dl>%s</dl></section>'
        % (
            _html_escape(table),
            rowid,
            _admin_href(admin_query, table=table),
            _html_escape(table),
            rows,
        )
    )


async def _admin_row_values(env, table, form):
    # Map submitted f_<col> fields to (column, value) for valid columns only,
    # encrypting the `data` column from its edited JSON.
    valid = set(await _admin_table_columns(env, table))
    cols, values = [], []
    for key, vals in form.items():
        if not key.startswith("f_"):
            continue
        col = key[2:]
        if col not in valid:
            continue
        raw = vals[0] if vals else ""
        if col == "data":
            parsed = json.loads(raw) if raw.strip() else {}
            if _admin_contains_wallet_key(parsed):
                raise ValueError("wallet_private_key_fields_are_not_accepted")
            values.append(await encrypt_row(env, parsed))
        else:
            values.append(raw)
        cols.append(col)
    return cols, values


async def _admin_update_row(env, table, form):
    return (
        "Update blocked: generic database mutation is disabled; use the "
        "purpose-built audited administration action."
    )


async def _admin_insert_row(env, table, form):
    return (
        "Insert blocked: generic database mutation is disabled; use the "
        "purpose-built audited administration action."
    )


async def _admin_selected_rows_have_wallet_keys(env, table, rowids):
    # A bulk delete must not become an undeclared custody scrub. Only tables
    # that can historically embed donation_secret are inspected; key-bearing
    # rows remain until the separately confirmed offline migration updates them.
    if table not in ("users", "nodes") or not rowids:
        return False
    for start in range(0, len(rowids), 90):
        batch = rowids[start:start + 90]
        placeholders = ",".join(["?"] * len(batch))
        rows = await d1_all(
            env,
            "SELECT data FROM " + table + " WHERE rowid IN ("
            + placeholders + ")",
            *batch,
        )
        for row in rows or []:
            decoded = await decrypt_row(env, row.get("data", ""))
            if _admin_contains_wallet_key(decoded):
                return True
    return False


async def _admin_selected_rows_digest(env, table, rowids):
    """Return a content-free audit digest for a bounded operational purge."""
    if not _admin_purge_allowed(table) or not rowids:
        return ""
    records = []
    for start in range(0, min(len(rowids), 500), 90):
        batch = rowids[start:start + 90]
        placeholders = ",".join(["?"] * len(batch))
        rows = await d1_all(
            env,
            "SELECT rowid AS _rowid_,* FROM " + table
            + " WHERE rowid IN (" + placeholders + ") ORDER BY rowid",
            *batch,
        )
        records.extend(rows or [])
    canonical = json.dumps(
        records,
        sort_keys=True,
        separators=(",", ":"),
        default=str,
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def _render_diag_breakdown(title, pairs):
    if not pairs:
        return ""
    rows = "".join(
        "<tr><td>%s</td><td>%s</td></tr>" % (_html_escape(k or "(unknown)"),
                                             _html_escape(n))
        for k, n in pairs)
    return ('<div class="diagcol"><h3>%s</h3><table><thead><tr>'
            '<th>%s</th><th>runs</th></tr></thead><tbody>%s</tbody></table></div>'
            % (_html_escape(title), _html_escape(title), rows))


def _render_install_diag_overview(summary):
    # Anonymous install funnel: per-step distinct-run counts (reached / ok /
    # failed) plus coarse platform breakdowns. Counts are over a 30-day window.
    total = summary.get("total", 0)
    completed = summary.get("completed", 0)
    funnel_rows = []
    for f in summary.get("funnel", []):
        runs = f["runs"]
        pct = ("%d%%" % round(100 * f["ok"] / runs)) if runs else "—"
        fail_cls = ' class="s5"' if f["failed"] else ""
        funnel_rows.append(
            "<tr><td>%s</td><td>%s</td><td>%s</td><td%s>%s</td><td>%s</td></tr>"
            % (_html_escape(f["step"]), _html_escape(runs), _html_escape(f["ok"]),
               fail_cls, _html_escape(f["failed"]), pct))
    funnel_table = (
        '<div class="diagcol"><h3>Install funnel (30d)</h3>'
        '<table><thead><tr><th>Step</th><th>Reached</th><th>OK</th>'
        '<th>Failed</th><th>OK %</th></tr></thead><tbody>'
        + "".join(funnel_rows) + "</tbody></table></div>")
    return (
        '<div class="title">Install diagnostics · %d run(s), %d completed</div>'
        '<div class="meta">Anonymous: each row is a random per-run id, never tied '
        'to an account, email, or IP. 30-day window.</div>'
        '<div class="diaggrid">%s%s%s%s</div>'
        % (total, completed, funnel_table,
           _render_diag_breakdown("Platform", summary.get("platforms", [])),
           _render_diag_breakdown("Pkg manager", summary.get("managers", [])),
           _render_diag_breakdown("Distro", summary.get("distros", [])))
    )


async def _render_table_view(
        env, table, csrf_field="", admin_query="", user_filter=""):
    # Generic "show all rows" view for one D1 table. The encrypted `data` column
    # (users/nodes/repos/inboxes store an AES-GCM blob there) is decrypted in place
    # so the admin can actually read it. The table name is validated by the
    # caller against the live table list, so it is safe to interpolate.
    # rowid lets the admin select + bulk-delete any row regardless of the table's
    # declared primary key (all these tables are rowid tables).
    # Newest-first by default: rowid ascends with insertion, so ORDER BY rowid DESC
    # surfaces the most recent rows (and, with LIMIT, keeps the newest 500) for
    # every table — including the error log, which reads from these rows.
    if table in ADMIN_HIDDEN_TABLES:
        return '<div class="empty">This table is restricted.</div>'
    requested_user = (
        clean_string(user_filter or "", MAX_NODE_NAME).strip().lower()
        if table == "users" else ""
    )
    if requested_user:
        requested_user_bi = await blind_index(env, requested_user)
        rows = await d1_all(
            env,
            "SELECT rowid AS _rowid_, * FROM users "
            "WHERE user_bi=? LIMIT 1",
            requested_user_bi,
        )
        total = len(rows)
    else:
        rows = await d1_all(
            env, "SELECT rowid AS _rowid_, * FROM " + table
            + " ORDER BY rowid DESC LIMIT 500")
        count_row = await d1_first(
            env, "SELECT COUNT(*) AS n FROM " + table)
        total = int((count_row or {}).get("n", 0) or 0)

    # Admin tool: reset any user account's login password. Shown above the
    # users table; posts back to ?action=set_password (handled in _admin).
    prefix = ""
    if table == "users":
        user_value = (
            ' value="%s"' % _html_escape(requested_user)
            if requested_user else ""
        )
        detail_heading = (
            '<div class="title" id="user-detail">User detail · %s · '
            '<a class="navlink" href="%s">Back to all users</a></div>'
            % (
                _html_escape(requested_user),
                _admin_href(admin_query, table="users"),
            )
            if requested_user else ""
        )
        prefix = (
            detail_heading +
            '<div class="tools">'
            '<form method="post" action="%s" '
            'onsubmit="return confirm(\'Set a new login password for this '
            'account?\')">'
            + csrf_field +
            '<input type="text" name="name" placeholder="user name"%s '
            'autocomplete="off" required>'
            '<input type="password" name="password" '
            'placeholder="new password (min 8 chars)" minlength="8" required>'
            '<button type="submit">Set password</button>'
            '</form>'
            '<span class="meta">Resets a user account\'s login password '
            '(PBKDF2-hashed); email and payout address are left unchanged.</span>'
            '<form method="post" action="%s" '
            'onsubmit="return confirm(\'Resend the verification email for this '
            'account?\')">'
            + csrf_field +
            '<input type="text" name="name" placeholder="user name"%s '
            'autocomplete="off" required>'
            '<button type="submit">Resend verify email</button>'
            '</form>'
            '<span class="meta">Re-sends the email-confirmation link to a user\'s '
            'stored address; queues it for manual verification if email is not '
            'configured.</span>'
            '</div>'
        ) % (
            _admin_href(admin_query, table="users", action="set_password"),
            user_value,
            _admin_href(admin_query, table="users", action="resend_verify"),
            user_value,
        )

    if table == "install_diag":
        # Purpose-built anonymous install funnel + recent events, newest first.
        summary = await install_diag_summary(env)
        recent = await d1_all(
            env,
            "SELECT rowid AS _rowid_, * FROM install_diag ORDER BY id DESC LIMIT 200")
        body = []
        for r in recent:
            ok = int(r.get("ok", 0) or 0)
            cls = "" if ok else "s5"
            body.append(
                "<tr%s>"
                % _admin_record_row_attrs(
                    admin_query, table, r.get("_rowid_", ""))
                + _admin_row_checkbox(r.get("_rowid_", ""))
                + '<td data-ts="%s">%s</td>'
                '<td>%s</td><td class="%s">%s</td><td>%s</td><td>%s</td>'
                "<td>%s</td><td>%s</td><td>%s</td></tr>"
                % (_html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                   _html_escape(r.get("step", "")), cls,
                   "ok" if ok else "fail",
                   _html_escape(r.get("os", "")), _html_escape(r.get("arch", "")),
                   _html_escape(r.get("pm", "")), _html_escape(r.get("distro", "")),
                   _html_escape(r.get("detail", "")))
            )
        if not body:
            events = '<div class="empty">No install events recorded yet.</div>'
        else:
            events = (_admin_bulk_form_open(table, csrf_field, admin_query)
                      + "<table><thead><tr>" + _admin_select_all_th()
                      + "<th>Time</th><th>Step</th><th>Result</th><th>OS</th>"
                      "<th>Arch</th><th>PM</th><th>Distro</th><th>Detail</th>"
                      "</tr></thead><tbody>" + "".join(body)
                      + "</tbody></table></form>")
        return (_render_install_diag_overview(summary)
                + '<div class="title">Recent events · %d of %d row(s)</div>'
                % (len(recent), total) + events)

    if table == "error_log":
        # Keep the purpose-built, time-formatted error view and add a bounded
        # 24-hour occurrence overview. The aggregation uses the same already
        # redacted fields as the raw admin-only table.
        now = int(Date.now())
        recent_24h = await d1_all(
            env,
            "SELECT ts,status,method,path,message,actor FROM error_log "
            "WHERE ts>=? ORDER BY ts DESC LIMIT 5000",
            now - 24 * 60 * 60 * 1000,
        )
        hourly = [0] * 24
        groups = {}
        for row in recent_24h or []:
            ts = int(row.get("ts") or 0)
            if 0 < ts < 10 ** 11:
                ts *= 1000
            age_hours = max(0, (now - ts) // (60 * 60 * 1000))
            if age_hours < 24:
                hourly[23 - int(age_hours)] += 1
            signature = (
                str(row.get("status") or ""),
                str(row.get("method") or "").upper(),
                str(row.get("path") or ""),
                str(row.get("message") or ""),
            )
            group = groups.setdefault(
                signature,
                {
                    "hours": [0] * 24,
                    "firstSeen": ts,
                    "lastSeen": ts,
                    # Accounts whose own sessions hit this exact failure, and
                    # how many occurrences carried no session at all. Grouping
                    # stays keyed on the failure itself so one user's traffic
                    # never splits a group; the affected users ride along.
                    "actors": {},
                    "anonymous": 0,
                },
            )
            group["firstSeen"] = min(group["firstSeen"] or ts, ts)
            group["lastSeen"] = max(group["lastSeen"] or ts, ts)
            actor = str(row.get("actor") or "").strip().lower()
            if actor:
                group["actors"][actor] = group["actors"].get(actor, 0) + 1
            else:
                group["anonymous"] += 1
            if age_hours < 24:
                group["hours"][23 - int(age_hours)] += 1
        peak = max(hourly) if hourly else 0
        bars = []
        for index, count in enumerate(hourly):
            height = max(2, round(132 * count / peak)) if peak else 2
            hours_ago = 23 - index
            label = (
                "current hour" if hours_ago == 0
                else "%d hours ago" % hours_ago
            )
            bars.append(
                '<div class="error-bar" tabindex="0" role="img" '
                'aria-label="%s: %d error%s" data-empty="%s" '
                'style="height:%dpx" title="%s · %d"></div>'
                % (
                    _html_escape(label),
                    count,
                    "" if count == 1 else "s",
                    "true" if count == 0 else "false",
                    height,
                    _html_escape(label),
                    count,
                )
            )
        group_rows = []
        for (status, request_method, path, message), group in sorted(
                groups.items(),
                key=lambda item: (-sum(item[1]["hours"]), item[0]))[:25]:
            frequency = group["hours"]
            count = sum(frequency)
            group_peak = max(frequency) if frequency else 0
            spark_bars = []
            for index, bucket_count in enumerate(frequency):
                height = (
                    max(2, round(28 * bucket_count / group_peak))
                    if group_peak else 2
                )
                hours_ago = 23 - index
                label = (
                    "current hour" if hours_ago == 0
                    else "%d hours ago" % hours_ago
                )
                spark_bars.append(
                    '<span class="error-spark-bar" data-empty="%s" '
                    'style="height:%dpx" title="%s · %d"></span>'
                    % (
                        "true" if bucket_count == 0 else "false",
                        height,
                        _html_escape(label),
                        bucket_count,
                    )
                )
            related_users = _admin_error_related_users(
                group["actors"], group["anonymous"])
            group_rows.append(
                '<tr><td>%d</td><td><div class="error-sparkline" '
                'tabindex="0" role="img" '
                'aria-label="24-hour frequency: %d occurrence%s">%s</div></td>'
                "<td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                "<td title=\"%s\"><span class=\"error-message\">%s</span>%s</td>"
                "%s"
                '<td data-ts="%s">%s</td><td data-ts="%s">%s</td>'
                "<td>%s</td><td>%s</td></tr>"
                % (
                    count,
                    count,
                    "" if count == 1 else "s",
                    "".join(spark_bars),
                    _admin_error_source_badge(request_method, path),
                    _html_escape(status or "error"),
                    _html_escape(request_method or "—"),
                    _html_escape(path or "—"),
                    _html_escape(message or "—"),
                    # A crash report earns its length: show all of it. The
                    # cell wraps, so the group stays readable at any size.
                    _html_escape(message or "—"),
                    _admin_error_copy_button(message or ""),
                    _admin_error_users_cell(
                        group["actors"], group["anonymous"]),
                    _html_escape(group["firstSeen"]),
                    _html_escape(group["firstSeen"]),
                    _html_escape(group["lastSeen"]),
                    _html_escape(group["lastSeen"]),
                    _admin_error_bot_task_form(
                        status, request_method, path, message, related_users,
                        csrf_field, admin_query),
                    _admin_error_group_delete_form(
                        status, request_method, path, message,
                        csrf_field, admin_query),
                )
            )
        analytics = (
            '<section class="error-analytics" aria-labelledby="error-analytics-title">'
            '<h2 id="error-analytics-title">Previous 24 hours · %d occurrence%s</h2>'
            '<div class="error-chart">%s</div>'
            '<div class="error-hours"><span>24h ago</span><span>12h ago</span>'
            '<span>now</span></div><div class="error-groups">'
            '<h3>Equivalent errors</h3>%s</div></section>'
            % (
                len(recent_24h or []),
                "" if len(recent_24h or []) == 1 else "s",
                "".join(bars),
                (
                    "<table><thead><tr><th>Count</th><th>24-hour frequency</th>"
                    "<th>Source</th><th>Status</th>"
                    "<th>Method</th><th>Path</th><th>Message</th>"
                    "<th>Related users</th>"
                    "<th>First seen</th><th>Last seen</th>"
                    "<th>Bot task</th><th>Delete</th></tr></thead>"
                    "<tbody>" + "".join(group_rows) + "</tbody></table>"
                    if group_rows
                    else '<div class="empty">No errors in the previous 24 hours.</div>'
                ),
            )
        )
        body = []
        for r in rows:
            status = r.get("status", "")
            cls = "s5" if str(status).startswith("5") else ""
            body.append(
                "<tr%s>"
                % _admin_record_row_attrs(
                    admin_query, table, r.get("_rowid_", ""))
                + _admin_row_checkbox(r.get("_rowid_", ""))
                + '<td data-ts="%s">%s</td>'
                "<td>%s</td>"
                '<td class="%s">%s</td>'
                "<td>%s</td><td>%s</td>"
                "<td><span class=\"error-message\">%s</span>%s</td>"
                "%s<td>%s</td><td>%s</td><td>%s</td></tr>"
                % (_html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                   _admin_error_source_badge(
                       r.get("method", ""), r.get("path", "")),
                   cls, _html_escape(status),
                   _html_escape(r.get("method", "")), _html_escape(r.get("path", "")),
                   _html_escape(r.get("message", "")),
                   _admin_error_copy_button(r.get("message", "")),
                   _admin_error_row_user_cell(r.get("actor", "")),
                   _html_escape(r.get("ray", "")),
                   _admin_error_row_bot_task_button(
                       r.get("_rowid_", ""), admin_query),
                   _admin_error_row_delete_button(
                       r.get("_rowid_", ""), admin_query))
            )
        if not body:
            inner = '<div class="empty">No errors recorded yet.</div>'
        else:
            inner = (_admin_bulk_form_open(table, csrf_field, admin_query)
                     + "<table><thead><tr>" + _admin_select_all_th()
                     + "<th>Time</th><th>Source</th><th>Status</th><th>Method</th>"
                     "<th>Path</th><th>Message</th><th>Related user</th>"
                     "<th>CF-Ray</th>"
                     "<th>Bot task</th><th>Delete</th></tr></thead><tbody>"
                     + "".join(body) + "</tbody></table></form>")
        return (
            '<div class="title">Error logs · %d row(s)</div>' % total
            + analytics
            + inner
        )

    purge_allowed = _admin_purge_allowed(table)
    add_link = ""
    if not rows:
        return (prefix
                + '<div class="title">%s · 0 rows%s</div>'
                  '<div class="empty">This table is empty.</div>'
                % (_html_escape(table), add_link))

    # Column order: union of keys, first row's order first. The synthetic
    # _rowid_ column drives row selection and is not displayed.
    columns = [c for c in rows[0].keys() if c != "_rowid_"]
    for r in rows:
        for k in r.keys():
            if k != "_rowid_" and k not in columns:
                columns.append(k)

    # Decrypt each row's `data` blob once, then promote the union of its
    # top-level JSON keys to real table columns so rows stay one line tall.
    # The data cell itself collapses to a marker with the pretty JSON in its
    # hover tooltip.
    decoded_rows = []
    for r in rows:
        decoded = None
        if isinstance(r.get("data"), str) and r.get("data"):
            decoded = await decrypt_row(env, r.get("data"))
        decoded_rows.append((
            r,
            _admin_redact_wallet_keys(decoded)
            if isinstance(decoded, dict) else None,
        ))
    json_cols = []
    for _r, decoded in decoded_rows:
        if decoded:
            for k in decoded:
                if k not in columns and k not in json_cols:
                    json_cols.append(k)
    json_cols = json_cols[:ADMIN_MAX_JSON_COLS]

    # Surface which columns the admin is looking at plaintext vs. ciphertext:
    # the `data` blob is AES-GCM encrypted at rest and only decrypted for this
    # view, so operators should not mistake a successful decrypt for the data
    # having been stored unencrypted.
    enc_notice = ""
    if "data" in columns:
        encrypted_total = sum(
            1 for r in rows if isinstance(r.get("data"), str) and r.get("data"))
        decrypted_ok = sum(
            1 for _r, decoded in decoded_rows if decoded is not None)
        plain_cols = [c for c in columns if c != "data"]
        sentence = (
            "🔒 <code>data</code> is stored AES-GCM encrypted and decrypted "
            "here for display (%d/%d row(s) decrypted)"
            % (decrypted_ok, encrypted_total)
        )
        if json_cols:
            sentence += ", expanded into " + ", ".join(
                "<code>%s</code>" % _html_escape(c) for c in json_cols)
        sentence += "."
        if plain_cols:
            sentence += " Column(s) " + ", ".join(
                "<code>%s</code>" % _html_escape(c) for c in plain_cols
            ) + " are stored as plaintext."
        enc_notice = '<div class="meta">%s</div>' % sentence
    else:
        enc_notice = (
            '<div class="meta">No encrypted columns in this table; all '
            'values are stored as plaintext.</div>'
        )

    body = []
    for r, decoded_data in decoded_rows:
        rid = r.get("_rowid_", "")
        cells = [_admin_row_checkbox(rid)] if purge_allowed else []
        for col in columns:
            value = r.get(col)
            if col == "data" and decoded_data is not None:
                cells.append(_admin_json_cell(decoded_data))
                continue
            cells.append(_admin_compact_cell(value))
        for col in json_cols:
            cells.append(_admin_compact_cell(
                None if decoded_data is None else decoded_data.get(col)))
        body.append(
            "<tr%s>%s</tr>"
            % (
                _admin_record_row_attrs(
                    admin_query, table, rid),
                "".join(cells),
            )
        )

    head = (_admin_select_all_th() if purge_allowed else "") + "".join(
        "<th>%s</th>" % _html_escape(c) for c in columns) + "".join(
        '<th class="jcol" title="from the data JSON">%s</th>' % _html_escape(c)
        for c in json_cols)
    table_markup = (
        '<table class="compact"><thead><tr>' + head + "</tr></thead><tbody>"
        + "".join(body) + "</tbody></table>"
    )
    if purge_allowed:
        table_markup = (
            _admin_bulk_form_open(table, csrf_field, admin_query)
            + table_markup
            + "</form>"
        )
    return (
        prefix
        + '<div class="title">%s · %d row(s)%s%s</div>'
        % (_html_escape(table), total,
           " (showing 500)" if total > 500 else "", add_link)
        + enc_notice
        + table_markup
    )


# (key, label, icon, red-when-nonzero). Order is the read order: what the
# platform is carrying right now, then what has gone wrong on it.
ADMIN_STAT_CARDS = (
    ("hosts", "Live hosts", "chart", False),
    ("clients", "Chat clients", "users", False),
    ("repos", "Catalog repos", "database", False),
    ("installs_24h", "Installs (24h)", "download", False),
    ("errors_total", "Errors (all time)", "alert", True),
    ("errors_24h", "Errors (24h)", "alert", True),
)


def _render_admin_stats(stats):
    if not stats:
        return ""
    tiles = []
    for key, label, icon_name, warn in ADMIN_STAT_CARDS:
        value = stats.get(key, 0) or 0
        tiles.append(admin_shell.kpi(
            label, admin_shell.format_int(value), icon_name=icon_name,
            tone="bad" if warn and value else ""))
    return '<div class="grid cols-3">' + "".join(tiles) + "</div>"


def _render_admin_operational_alerts(settings, csrf_field="", admin_query=""):
    settings = settings if isinstance(settings, dict) else {}
    rows = []
    for system_id, label in STATUS_ALERT_ADMIN_SYSTEMS:
        field_id = system_id.replace(":", "__").replace("*", "all")
        ping_checked = (
            " checked" if _status_monitor_alert_setting(
                settings, system_id, "pings") else "")
        email_checked = (
            " checked" if _status_monitor_alert_setting(
                settings, system_id, "emails") else "")
        continual_checked = (
            " checked" if _status_monitor_alert_setting(
                settings, system_id, "continual") else "")
        rows.append(
            '<tr><th scope="row">%s</th>'
            '<td><label><input type="checkbox" name="ping_%s" value="1"%s> '
            'Ping</label></td>'
            '<td><label><input type="checkbox" name="email_%s" value="1"%s> '
            'Email</label></td>'
            '<td><label><input type="checkbox" name="continual_%s" value="1"%s> '
            'Continual ping</label></td></tr>' % (
                _html_escape(label), field_id, ping_checked,
                field_id, email_checked, field_id, continual_checked))
    return (
        '<section id="operational-alerts" class="admin-setting" '
        'tabindex="-1"><div><h2>Operational alerts</h2>'
        '<p>Admin-only delivery controls for every /status monitor. Continual '
        'ping repeats a still-down alert every five minutes; the homepage is '
        'selected by default. Optional attention emails include a redacted '
        'two-minute Cloudflare log excerpt when credentials are configured.</p>'
        '</div>'
        '<form method="post" action="%s">' %
        _admin_href(admin_query, action="set_operational_alerts",
                    view="alerts") +
        csrf_field +
        '<div class="table-wrap"><table><thead><tr><th>Monitor</th>'
        '<th>Qt / Pings</th><th>Email</th><th>Persistent outage</th>'
        '</tr></thead><tbody>' + "".join(rows) + '</tbody></table></div>'
        '<button type="submit">Save alert settings</button></form></section>'
    )


def _render_admin_repo_terms_flags(csrf_field="", admin_query=""):
    return (
        '<section id="repository-terms-flags" class="admin-setting" '
        'tabindex="-1"><div><h2>Repository Terms flags</h2>'
        '<p>Apply or clear the public policy-warning badge for a repository. '
        'The operator note is encrypted and remains admin-only.</p></div>'
        '<form method="post" action="%s">' %
        _admin_href(admin_query, action="set_repo_terms_flag",
                    view="controls") +
        csrf_field +
        '<input type="text" name="owner" placeholder="owner" '
        'pattern="[A-Za-z0-9._-]{1,100}" required>'
        '<input type="text" name="repo" placeholder="repository" '
        'pattern="[A-Za-z0-9._-]{1,100}" required>'
        '<select name="category" aria-label="Terms category">'
        '<option value="spam">Spam</option>'
        '<option value="malware">Malware</option>'
        '<option value="harassment">Harassment</option>'
        '<option value="illegal">Illegal content</option>'
        '<option value="other">Other</option></select>'
        '<input type="text" name="note" maxlength="500" '
        'placeholder="private operator note">'
        '<label><input type="checkbox" name="clear" value="1"> Clear</label>'
        '<button type="submit">Save flag</button></form></section>'
    )


def _render_admin_operator_tools(csrf_field="", admin_query=""):
    """The two console actions that are neither a setting nor a table read."""
    return (
        '<section class="admin-setting"><div><h2>Legacy custody status</h2>'
        '<p>Worker signing and automated sweeps are disabled. Historical '
        'encrypted deposit rows require an offline, balance-reconciled '
        'migration; this console cannot access or use wallet seeds.</p></div>'
        '<form method="post" action="%s" '
        'onsubmit="return confirm(\'Show legacy custody migration status?\')">'
        % _admin_href(admin_query, action="disburse", view="controls")
        + csrf_field
        + '<button type="submit">Legacy custody status</button></form>'
          '</section>'
        '<section class="admin-setting"><div><h2>Ownership transfer</h2>'
        '<p>Parks a pending transfer on the node\'s own account record — it '
        'only completes once that node\'s current owner approves the '
        'confirmation prompt on its own client.</p></div>'
        '<form method="post" action="%s" '
        'onsubmit="return confirm(\'Request ownership transfer for this node?\')">'
        % _admin_href(admin_query, action="request_ownership", view="controls")
        + csrf_field
        + '<input type="text" name="target" placeholder="node to take (name)" '
          'autocomplete="off" required>'
          '<input type="text" name="owner" '
          'placeholder="new owner (account name)" autocomplete="off" required>'
          '<button type="submit">Request ownership transfer</button></form>'
          '</section>')


# Behaviour the section renderers depend on, unchanged from the single-page
# console: absolute timestamps rendered in the viewer's own locale with a
# live relative suffix, and copy-to-clipboard on error messages. The shell
# itself stays script-free; this rides along as the page's body_extra.
ADMIN_SCRIPT = (
    "<script>const adminTimes=[];"
    "for(const el of document.querySelectorAll('[data-ts]')){"
    "let ms=Number(el.getAttribute('data-ts'));if(ms&&ms<1e11)ms*=1000;"
    "if(!ms)continue;el.textContent=new Date(ms).toLocaleString();"
    "const rel=document.createElement('span');rel.className='relative-time';"
    "el.append(' · ',rel);adminTimes.push([rel,ms]);}"
    "function updateAdminRelativeTimes(){const now=Date.now();"
    "for(const pair of adminTimes){const rel=pair[0],ms=pair[1];"
    "const future=ms>now,seconds=Math.max(0,Math.floor(Math.abs(now-ms)/1000));"
    "let value,unit;if(seconds<60){value=seconds;unit='second';}"
    "else if(seconds<3600){value=Math.floor(seconds/60);unit='minute';}"
    "else if(seconds<86400){value=Math.floor(seconds/3600);unit='hour';}"
    "else{value=Math.floor(seconds/86400);unit='day';}"
    "rel.textContent=future?'in '+value+' '+unit+(value===1?'':'s'):"
    "value+' '+unit+(value===1?'':'s')+' ago';}}"
    "updateAdminRelativeTimes();setInterval(updateAdminRelativeTimes,30000);"
    # Copy-to-clipboard for error messages. Delegated so the analytics and
    # raw tables share one handler, and written to survive the non-secure
    # contexts / older browsers where navigator.clipboard is absent.
    "document.addEventListener('click',function(ev){"
    "const btn=ev.target.closest&&ev.target.closest('[data-copy]');"
    "if(!btn)return;ev.preventDefault();ev.stopPropagation();"
    "const text=btn.getAttribute('data-copy')||'';"
    "const label=btn.dataset.copyLabel||btn.textContent;"
    "btn.dataset.copyLabel=label;"
    "function done(ok){btn.textContent=ok?'Copied':'Copy failed';"
    "setTimeout(function(){btn.textContent=label;},1200);}"
    "function fallback(){try{const ta=document.createElement('textarea');"
    "ta.value=text;ta.setAttribute('readonly','');"
    "ta.style.position='fixed';ta.style.opacity='0';"
    "document.body.appendChild(ta);ta.select();"
    "const ok=document.execCommand('copy');ta.remove();done(ok);}"
    "catch(e){done(false);}}"
    "if(navigator.clipboard&&navigator.clipboard.writeText){"
    "navigator.clipboard.writeText(text).then(function(){done(true);},"
    "fallback);}else{fallback();}});"
    "if(location.hash==='#operational-alerts'){"
    "var a=document.getElementById('operational-alerts');"
    "if(a){a.scrollIntoView({block:'center'});a.focus({preventScroll:true});}"
    "}</script>")


def _render_admin_nav(tables, active, counts=None, admin_query="", sort_records=False):
    counts = counts or {}
    if sort_records:
        tables = sorted(tables, key=lambda t: counts.get(t, 0), reverse=True)
    toggle_label = "A–Z" if sort_records else "Sort by records"
    toggle_href = _admin_href(admin_query, table=active,
                              sort=("name" if sort_records else "records"))
    links = ['<div class="sec">Tables <a class="navsort" href="%s">%s</a></div>'
             % (toggle_href, toggle_label)]
    for t in tables:
        label = "Error logs" if t == "error_log" else t
        cls = ' class="active"' if t == active else ""
        n = counts.get(t)
        suffix = (' <span class="navcount">%d</span>' % n) if n is not None else ""
        # Carry the active sort along: sort state lives only in the URL, so a
        # table link that dropped it would silently reset the nav to the
        # most-records-first default.
        links.append('<a href="%s"%s>%s%s</a>'
                     % (_admin_href(admin_query, table=t,
                                    sort=("records" if sort_records else "name")),
                        cls, _html_escape(label), suffix))
    return "<nav>" + "".join(links) + "</nav>"


# The console's sections, in sidebar order. Every one of them existed before
# as a band on a single very long document; the split is what lets the shell's
# grouped navigation mean anything, and it is the only thing "view" changes —
# every form still posts to the same URL with the same action.
ADMIN_VIEWS = ("overview", "errors", "installs", "alerts", "controls",
               "database")


def normalize_admin_view(value):
    """The requested section, or the default ``overview``."""
    value = str(value or "").strip().lower()
    return value if value in ADMIN_VIEWS else "overview"


def _admin_view_content(view, env_stats, tables, active_table, table_html,
                        counts=None, csrf_field="", admin_query="",
                        sort_records=False, operational_alert_settings=None):
    """The one section body the shell wraps. Never the whole console."""
    if view == "alerts":
        return (
            admin_shell.page_head(
                "Alert delivery",
                "Administrator-only delivery controls for every /status "
                "monitor.")
            + _render_admin_operational_alerts(
                operational_alert_settings, csrf_field, admin_query))
    if view == "controls":
        return (
            admin_shell.page_head(
                "Controls",
                "Operator actions that change what the public site shows or "
                "who owns a node.")
            + _render_admin_repo_terms_flags(csrf_field, admin_query)
            + _render_admin_operator_tools(csrf_field, admin_query))
    if view in ("errors", "installs", "database"):
        # All three are the same generic browser pointed at a different table;
        # only Database offers the table list, because the other two are one
        # named table each and a picker there would just be a way to leave.
        body = admin_shell.page_head(*{
            "errors": ("Errors",
                       "Every logged Worker error, newest first, grouped by "
                       "message."),
            "installs": ("Install diagnostics",
                         "Anonymous desktop install funnel: per-run steps, "
                         "platforms and package managers."),
            "database": ("Database",
                         "Every D1 table this deployment has, with encrypted "
                         "row payloads decrypted in place."),
        }[view])
        if view == "database":
            return (body + '<div class="layout">'
                    + _render_admin_nav(tables, active_table, counts,
                                        admin_query, sort_records)
                    + "<main>" + table_html + "</main></div>")
        return body + '<div class="layout-single">' + table_html + "</div>"
    return (
        admin_shell.page_head(
            "Overview",
            "Live Durable Object load, catalog size, and recent error volume.")
        + _render_admin_stats(env_stats))


def render_admin_html(env_stats, tables, active_table, table_html, banner="",
                      counts=None, csrf_field="", admin_query="",
                      sort_records=False, operational_alert_settings=None,
                      view="overview", console="", theme="system",
                      badges=None, account="", theme_action="",
                      theme_fields="", search_value=""):
    """One admin page: the shared chrome plus exactly one section."""
    view = normalize_admin_view(view)
    content = (
        (admin_shell.banner(banner, admin_shell.banner_tone(banner))
         if banner else "")
        + _admin_view_content(
            view, env_stats, tables, active_table, table_html, counts,
            csrf_field, admin_query, sort_records,
            operational_alert_settings))
    return admin_shell.render_page(
        ADMIN_VIEW_TITLES[view],
        # The legacy rules are all scoped under .ab-root; the shell draws its
        # own chrome outside it, so the two stylesheets cannot reach each
        # other's markup.
        '<div class="ab-root">' + content + "</div>",
        active=view,
        console=console,
        theme=theme,
        badges=badges,
        account=account,
        theme_action=theme_action,
        theme_fields=theme_fields,
        search_value=search_value,
        # The legacy rules are scoped under .ab-root and the shell draws its
        # own chrome, so the old stylesheet rides along only for the table,
        # form and diagnostics markup the section renderers still emit.
        # Analytics rides along too: every Worker-generated page loads it, and
        # this one losing it when it moved into the shell would be a silent
        # hole rather than a decision.
        head_extra=('<script src="/posthog.js"></script>'
                    "<style>" + ADMIN_STYLE + "</style>"),
        body_extra=ADMIN_SCRIPT,
    )


ADMIN_VIEW_TITLES = {
    "overview": "Overview",
    "errors": "Errors",
    "installs": "Install diagnostics",
    "alerts": "Alert delivery",
    "controls": "Controls",
    "database": "Database",
}
