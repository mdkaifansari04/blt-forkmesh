#pragma once

// Self-contained coding agent used by ForkMesh's "Cloudflare AI" provider
// (adhoc #1634).
//
// Cloudflare Workers AI models used to be composer chat models only: picking
// one sent the typed prompt to the relay's POST /api/ai/ask and showed the
// answer. This script gives them the same agent harness the Claude API
// provider has (ClaudeAgentScript.h): AgentRunner materializes it at runtime,
// runs it with the system python3 inside the session worktree, and it drives a
// minimal tool-use loop whose `bash` tool lets the model read and edit files
// and commit to the session branch. The model itself runs on the relay — the
// script POSTs each turn (conversation + tool declarations, OpenAI function
// format) to /api/ai/agent and executes the returned tool calls locally.
//
// The desktop has no session token, so AgentRunner injects a per-run Ed25519
// ticket as FORKMESH_AI_AGENT_AUTH (JSON: url/node/ts/sig, signed by this
// node's account key); the relay verifies it on every turn and applies its
// own per-account rate window. ForkMesh captures stdout as the session log
// and diffs the worktree afterward to build the patch.
//
// Only the Python standard library is used, so no `pip install` is needed.

#include <QString>

inline QString forkmeshCloudflareAgentScript()
{
    return QString::fromUtf8(R"PYAGENT(#!/usr/bin/env python3
"""Minimal Workers AI coding agent for ForkMesh (relay /api/ai/agent, stdlib only)."""
import json
import os
import select
import shlex
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

MAX_TURNS = 60
TOOL_OUTPUT_LIMIT = 30000
# forkmesh.com sits behind Cloudflare's browser-integrity check, which bans the
# stdlib default "Python-urllib/3.x" signature: the edge answers 403 with
# "error code: 1010" before the relay Worker ever runs, which used to read as a
# rejected run ticket (adhoc #1619). Identify as ForkMesh instead.
USER_AGENT = "ForkMesh-AI-agent/1.0 (+https://forkmesh.com)"
# Turns the relay throttled: how often to retry one turn before giving up.
RATE_LIMIT_RETRIES = 3
RATE_LIMIT_MAX_WAIT = 120
# Turns the relay failed outright (5xx) or that never reached it. The relay is
# a Cloudflare Worker whose Python isolate can be wedged by entirely unrelated
# traffic sharing it, which answers one turn with a 1101 "Worker threw
# exception" 500 while the next lands on a healthy isolate and works (adhoc
# #1633). Throwing away a run that is already mid-task over that is the wrong
# trade, so retry with a widening backoff.
TRANSIENT_RETRIES = 4
TRANSIENT_MAX_WAIT = 30
# Consecutive turns whose every tool call was a command this run already ran.
# The small instruction-tuned models behind this provider will otherwise loop
# on one command forever: llama-3.3 kept re-issuing the same
# `echo ... >> README.md` — appending the line again each turn, and only
# stopping when the person watching pressed Stop (adhoc #1622). Three such
# turns end the run instead, keeping whatever was already committed.
STALLED_TURN_LIMIT = 3
# How many times a model that stops early — no tool call, and either nothing to
# say or edits it never committed — is told to finish the job before the run
# ends anyway.
UNFINISHED_NUDGE_LIMIT = 2

# Claude needs no coaching to finish an agent loop; a 70B instruct model does.
# Spell out the two things it keeps getting wrong — that exit code 0 means the
# command worked, and that finishing means answering with no tool call.
SYSTEM_PROMPT = (
    "You are a coding agent working in a checked-out git worktree, already on "
    "the branch for this task. Use the `bash` tool for every action, one "
    "command per call.\n"
    "Rules:\n"
    "- Read each tool result before acting. \"(exit code 0)\" means that "
    "command already succeeded, so never send it a second time.\n"
    "- Before appending text to a file, check whether it is already there "
    "(for example `grep -F \"the text\" FILE`), so nothing is added twice.\n"
    "- Make the smallest change that satisfies the request, then verify it "
    "(for example `tail -n 5 FILE`).\n"
    "- Commit the work on the current branch with `git add` and "
    "`git commit -m`. The git identity is already configured: do not change "
    "it, do not create a branch, and do not push.\n"
    "- When the change is committed, stop calling tools and reply with one "
    "short sentence saying what you did."
)


def log(text):
    sys.stdout.write(text)
    sys.stdout.write("\n")
    sys.stdout.flush()


def read_prompt():
    # Prompt may arrive as an argument path or on stdin.
    for arg in sys.argv[1:]:
        if arg and os.path.isfile(arg):
            with open(arg, "r", encoding="utf-8") as handle:
                return handle.read()
    data = sys.stdin.read()
    return data


def pending_user_input():
    """Steering text ForkMesh has written to our stdin, or "" for none.

    The task prompt arrives as a file argument, so stdin only ever carries the
    instructions typed into the running session's steering box. Reading it must
    never block the loop, so poll and drain whatever is buffered right now.
    """
    try:
        fd = sys.stdin.fileno()
    except (AttributeError, ValueError, OSError):
        return ""
    chunks = []
    while True:
        try:
            # select() on a pipe is POSIX-only; on Windows this raises and the
            # run simply carries on without steering.
            if not select.select([fd], [], [], 0)[0]:
                break
            data = os.read(fd, 65536)
        except Exception:
            break
        if not data:
            break
        chunks.append(data.decode("utf-8", "replace"))
    return "".join(chunks).strip()


def prepare_git_environment():
    """Keep this run's git writes out of the person's own ~/.gitconfig.

    Told to commit, these models reach for `git config --global user.email`
    first — adhoc #1622 watched llama rewrite the desktop user's git identity
    to its own. Point GIT_CONFIG_GLOBAL at a scratch file seeded with the
    identity git already resolves here, so committing works out of the box and
    that instinct, when it strikes anyway, stays inside the run.
    """
    identity = []
    for key in ("name", "email"):
        try:
            proc = subprocess.run(
                ["git", "config", "--get", "user." + key],
                capture_output=True,
                text=True,
                timeout=30,
            )
        except Exception:
            continue
        value = (proc.stdout or "").strip().splitlines()
        if proc.returncode == 0 and value and value[0].strip():
            identity.append((key, value[0].strip()))
    if not identity:
        identity = [("name", "ForkMesh Agent"),
                    ("email", "agent@forkmesh.local")]
    path = os.path.join(
        tempfile.mkdtemp(prefix="forkmesh-cf-agent-git-"), "config")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("[user]\n")
        for key, value in identity:
            handle.write("\t%s = %s\n" % (key, value))
    os.environ["GIT_CONFIG_GLOBAL"] = path
    # A stray `git push` must fail rather than sit on a credential prompt for
    # the whole 600s tool timeout.
    os.environ["GIT_TERMINAL_PROMPT"] = "0"


def command_key(command):
    """What makes two tool calls "the same command" for the repeat guard.

    `echo 'hello' >> README.md` and `echo "hello" >> README.md` are one
    command spelled two ways, and llama-3.3 alternates between the spellings —
    which slipped straight past a raw string comparison and appended the line
    twice (adhoc #1633). Compare the shell tokens instead; a command shlex
    cannot parse falls back to its stripped text.
    """
    try:
        return tuple(shlex.split(command))
    except ValueError:
        return (command.strip(),)


def uncommitted_changes():
    """`git status --porcelain` for the worktree, or "" when it is clean."""
    try:
        proc = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            timeout=60,
            stdin=subprocess.DEVNULL,
        )
    except Exception:
        return ""
    if proc.returncode != 0:
        return ""
    # rstrip only: porcelain's first column is a space for a change that is
    # unstaged (" M README.md"), and stripping it turns the status the model
    # is shown into a different one.
    return (proc.stdout or "").rstrip()


def run_bash(command):
    try:
        proc = subprocess.run(
            command,
            shell=True,
            capture_output=True,
            text=True,
            timeout=600,
            # Our own stdin carries the session's steering text, and a command
            # that waits on input would sit there for the full timeout: every
            # tool call reads from /dev/null instead.
            stdin=subprocess.DEVNULL,
        )
    except subprocess.TimeoutExpired:
        return "Command timed out after 600s."
    out = (proc.stdout or "") + (proc.stderr or "")
    if len(out) > TOOL_OUTPUT_LIMIT:
        out = out[:TOOL_OUTPUT_LIMIT] + "\n...[output truncated]..."
    # Say so out loud: a bare exit line reads to a small model as "nothing
    # happened", which is half of why they re-run a command that worked.
    if not out.strip():
        out = "(no output)"
    return "(exit code %d)\n%s" % (proc.returncode, out)


# The one tool, in the OpenAI function format /api/ai/agent passes through to
# Workers AI. Same contract as the Claude agent script's bash tool.
TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "bash",
            "description": (
                "Run a shell command in the current working directory (the "
                "checked-out worktree). Use it to read, search, and edit files "
                "needed for the issue. After completing and verifying the "
                "requested changes, commit them to the current agent branch. "
                "Do not push or use the network."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "command": {
                        "type": "string",
                        "description": "The shell command to execute.",
                    }
                },
                "required": ["command"],
            },
        },
    }
]


def call_relay(auth, model, max_tokens, messages):
    body = json.dumps(
        {
            "nodeName": auth.get("node", ""),
            "ts": auth.get("ts", ""),
            "sig": auth.get("sig", ""),
            "model": model,
            "max_tokens": max_tokens,
            "tools": TOOLS,
            "messages": messages,
        }
    ).encode("utf-8")
    request = urllib.request.Request(auth["url"], data=body, method="POST")
    request.add_header("content-type", "application/json")
    request.add_header("accept", "application/json")
    request.add_header("user-agent", USER_AGENT)
    with urllib.request.urlopen(request, timeout=600) as response:
        return json.loads(response.read().decode("utf-8"))


def edge_blocked(detail):
    """True when Cloudflare's edge answered, not the relay Worker.

    Those blocks come back as a bare "error code: NNNN" line or a challenge
    page, never the relay's JSON, so the account behind the run ticket is not
    the problem and re-signing in cannot help.
    """
    low = (detail or "").lower()
    return "error code: 10" in low or ("cloudflare" in low and "<html" in low)


# Network-traffic markers. ForkMesh's agent detail page parses lines beginning
# with "==> [net]" to draw a live graphic of API calls and token flow, so keep
# the in=/out= fields machine-readable.
def net_request(turn, host, model, msg_count):
    log("==> [net] \U0001F310 request #%d → POST %s/api/ai/agent "
        "(model=%s, messages=%d)" % (turn, host, model, msg_count))


def net_response(turn, usage, tool_calls):
    inp = int(usage.get("inputTokens") or 0)
    out = int(usage.get("outputTokens") or 0)
    log("==> [net] \U0001F310 response #%d ← in=%d out=%d stop=%s"
        % (turn, inp, out, "tool_use" if tool_calls else "end"))


def net_error(turn, code):
    log("==> [net] \U0001F310 error #%d ← HTTP %s" % (turn, code))


def main():
    try:
        auth = json.loads(os.environ.get("FORKMESH_AI_AGENT_AUTH", "") or "{}")
    except ValueError:
        auth = {}
    if not isinstance(auth, dict) or not auth.get("url") or not auth.get("sig"):
        log("!! No signed relay ticket. Sign in to this node's account so "
            "ForkMesh can authorize Cloudflare AI agent runs.")
        return 1
    host = auth["url"].split("/")[2] if "//" in auth["url"] else auth["url"]

    model = os.environ.get("FORKMESH_AGENT_MODEL", "").strip()
    try:
        max_tokens = int(os.environ.get("FORKMESH_AGENT_MAX_OUTPUT_TOKENS", "2000"))
    except ValueError:
        max_tokens = 2000
    max_tokens = max(256, max_tokens)

    prompt = read_prompt().strip()
    if not prompt:
        log("!! Empty prompt; nothing to do.")
        return 1

    log("==> Cloudflare AI agent (model %s) talking to the relay at %s."
        % (model or "relay default", host))
    prepare_git_environment()
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": prompt},
    ]
    # Loop guards (see STALLED_TURN_LIMIT): every command this run has run
    # (keyed by command_key), the one it ran last, and how many turns in a row
    # have added nothing new. `nudges` is the separate early-stop budget.
    ran = {}
    last_key = ()
    stalled_turns = 0
    nudges = 0

    for turn in range(1, MAX_TURNS + 1):
        steer = pending_user_input()
        if steer:
            log("\n==> Steering: %s" % steer)
            messages.append({"role": "user", "content": steer})
            # A fresh instruction deserves a fresh run at the turn budget.
            stalled_turns = 0
        data = None
        rate_retries = RATE_LIMIT_RETRIES
        transient_retries = TRANSIENT_RETRIES
        transient_backoff = 2
        for _attempt in range(RATE_LIMIT_RETRIES + TRANSIENT_RETRIES + 1):
            net_request(turn, host, model or "default", len(messages))
            try:
                data = call_relay(auth, model, max_tokens, messages)
                break
            except urllib.error.HTTPError as err:
                detail = err.read().decode("utf-8", "replace")
                if err.code == 429 and rate_retries > 0:
                    rate_retries -= 1
                    try:
                        wait_ms = int(json.loads(detail).get("retryAfterMs") or 0)
                    except (ValueError, AttributeError):
                        wait_ms = 0
                    wait = min(RATE_LIMIT_MAX_WAIT, max(5, wait_ms // 1000))
                    net_error(turn, err.code)
                    log("==> Relay rate window reached; retrying in %ds." % wait)
                    time.sleep(wait)
                    continue
                # A 5xx is this one request failing, not the run being turned
                # away: the relay's shared Python isolate can be wedged by
                # unrelated traffic, and the retry usually lands elsewhere and
                # works (see TRANSIENT_RETRIES). The conversation is unchanged,
                # so re-sending it costs nothing but the turn.
                if err.code >= 500 and transient_retries > 0:
                    transient_retries -= 1
                    net_error(turn, err.code)
                    log("==> Relay error %d; retrying this turn in %ds (%d "
                        "%s left)."
                        % (err.code, transient_backoff, transient_retries + 1,
                           "try" if transient_retries == 0 else "tries"))
                    time.sleep(transient_backoff)
                    transient_backoff = min(TRANSIENT_MAX_WAIT,
                                            transient_backoff * 2)
                    continue
                net_error(turn, err.code)
                log("!! Relay AI agent error %d: %s" % (err.code, detail))
                if edge_blocked(detail):
                    log("!! Cloudflare's edge blocked this request before it "
                        "reached the relay, so the account sign-in is fine. "
                        "Update ForkMesh to a build whose agent turns send a "
                        "ForkMesh user agent.")
                elif err.code in (401, 403):
                    log("!! This node is not authorized to sign for the "
                        "account (or the run ticket expired).")
                elif err.code == 404:
                    log("!! This relay does not run AI agents yet (it needs a "
                        "newer deployment).")
                elif err.code >= 500:
                    log("!! The relay failed this turn %d times in a row; "
                        "anything the run already committed is kept."
                        % (TRANSIENT_RETRIES + 1))
                return 1
            except urllib.error.URLError as err:
                # Same call: a dropped connection mid-run is worth retrying
                # before abandoning the task.
                if transient_retries > 0:
                    transient_retries -= 1
                    net_error(turn, "unreachable")
                    log("==> Could not reach the relay (%s); retrying this "
                        "turn in %ds." % (err.reason, transient_backoff))
                    time.sleep(transient_backoff)
                    transient_backoff = min(TRANSIENT_MAX_WAIT,
                                            transient_backoff * 2)
                    continue
                net_error(turn, "unreachable")
                log("!! Could not reach the relay: %s" % err.reason)
                return 1
        if data is None:
            log("!! The relay stayed rate-limited; stopping this run.")
            return 1

        reply = (data.get("reply") or "").strip()
        tool_calls = data.get("toolCalls") or []
        answered = (data.get("model") or "").strip()
        if answered and answered != model:
            # The relay fell back (an unlisted pick, a retired id); adopt the
            # model that actually answered so the whole run stays on it.
            log("==> Relay fell back from %s to %s." % (model or "default", answered))
            model = answered
        net_response(turn, data.get("usage") or {}, tool_calls)

        if reply:
            log("\n● %s" % reply)  # ● assistant message

        # Echo the assistant turn back in the OpenAI wire shape the relay
        # validates, then execute each tool call locally.
        assistant = {"role": "assistant", "content": reply}
        results = []
        progressed = False
        for index, call in enumerate(tool_calls):
            name = call.get("name") or ""
            arguments = call.get("arguments") or {}
            call_id = call.get("id") or ("call_%d" % index)
            assistant.setdefault("tool_calls", []).append(
                {
                    "id": call_id,
                    "type": "function",
                    "function": {
                        "name": name,
                        "arguments": json.dumps(arguments),
                    },
                }
            )
            if name == "bash":
                command = arguments.get("command", "")
                if not command.strip():
                    result = ("No command was given. Put the shell command in "
                              "the `command` argument.")
                    log("\n!! %s" % result)
                elif command_key(command) == last_key:
                    # Answer an immediate repeat from the first run instead of
                    # executing it again: re-running `>> file` would append the
                    # same line twice, and the model needs telling, not obeying.
                    earlier_turn, result = ran[last_key]
                    log("\n$ %s" % command)
                    log("==> Not run again: this is the command from turn %d. "
                        "Its result is being reported unchanged."
                        % earlier_turn)
                    result = (
                        "This is the same command you already ran on turn %d, "
                        "so it was not run again. Its result was:\n%s\n"
                        "Do not send it a third time. If the task is done, "
                        "reply with a one-sentence summary and no tool call; "
                        "otherwise run a different command."
                        % (earlier_turn, result))
                else:
                    key = command_key(command)
                    log("\n$ %s" % command)
                    result = run_bash(command)
                    log(result)
                    if key not in ran:
                        progressed = True
                    ran[key] = (turn, result)
                    last_key = key
            else:
                result = "Unknown tool: %s. Only `bash` is available." % name
                log("\n!! %s" % result)
            results.append(
                {
                    "role": "tool",
                    "tool_call_id": call_id,
                    "name": name,
                    "content": result,
                }
            )
        messages.append(assistant)
        if results:
            messages.extend(results)
            stalled_turns = 0 if progressed else stalled_turns + 1
            if stalled_turns >= STALLED_TURN_LIMIT:
                log("\n!! The model spent %d turns re-running commands it had "
                    "already run, so this run is stopping here. Anything it "
                    "committed is kept." % stalled_turns)
                return 0
            continue

        # No tool call: the model considers itself done. Take it at its word
        # only if the worktree agrees. llama-3.3 routinely edits a file and
        # then stops without committing — and sometimes stops on an empty
        # answer, which used to log "Agent finished" over a run that had done
        # nothing at all (adhoc #1633). Say what is missing and let it finish.
        dirty = uncommitted_changes()
        if (dirty or not reply) and nudges < UNFINISHED_NUDGE_LIMIT:
            nudges += 1
            if dirty:
                note = (
                    "You stopped calling tools, but this worktree still has "
                    "uncommitted changes:\n%s\nCommit them on the current "
                    "branch (`git add -A` then `git commit -m \"...\"`), or "
                    "undo them with `git checkout -- .` if they are wrong. "
                    "Then reply with one short sentence and no tool call."
                    % dirty)
                log("\n==> Not finished: the worktree has uncommitted "
                    "changes. Asking the model to commit them.")
            else:
                note = (
                    "You sent neither a tool call nor an answer. If the task "
                    "is done, reply with one short sentence saying what you "
                    "did; otherwise call the `bash` tool to carry on.")
                log("\n==> Empty turn (no tool call, no answer). Asking the "
                    "model again.")
            messages.append({"role": "user", "content": note})
            continue
        if dirty:
            log("\n!! The model stopped without committing; the worktree "
                "changes below are kept as this run's patch.\n%s" % dirty)
        log("\n==> Agent finished.")
        return 0

    log("\n!! Reached the maximum number of turns without finishing.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
)PYAGENT");
}
