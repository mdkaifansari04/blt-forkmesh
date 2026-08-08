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
import subprocess
import sys
import time
import urllib.error
import urllib.request

MAX_TURNS = 60
TOOL_OUTPUT_LIMIT = 30000
# Turns the relay throttled: how often to retry one turn before giving up.
RATE_LIMIT_RETRIES = 3
RATE_LIMIT_MAX_WAIT = 120


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


def run_bash(command):
    try:
        proc = subprocess.run(
            command,
            shell=True,
            capture_output=True,
            text=True,
            timeout=600,
        )
    except subprocess.TimeoutExpired:
        return "Command timed out after 600s."
    out = (proc.stdout or "") + (proc.stderr or "")
    if len(out) > TOOL_OUTPUT_LIMIT:
        out = out[:TOOL_OUTPUT_LIMIT] + "\n...[output truncated]..."
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
    with urllib.request.urlopen(request, timeout=600) as response:
        return json.loads(response.read().decode("utf-8"))


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
    messages = [{"role": "user", "content": prompt}]

    for turn in range(1, MAX_TURNS + 1):
        data = None
        for attempt in range(RATE_LIMIT_RETRIES + 1):
            net_request(turn, host, model or "default", len(messages))
            try:
                data = call_relay(auth, model, max_tokens, messages)
                break
            except urllib.error.HTTPError as err:
                detail = err.read().decode("utf-8", "replace")
                if err.code == 429 and attempt < RATE_LIMIT_RETRIES:
                    try:
                        wait_ms = int(json.loads(detail).get("retryAfterMs") or 0)
                    except (ValueError, AttributeError):
                        wait_ms = 0
                    wait = min(RATE_LIMIT_MAX_WAIT, max(5, wait_ms // 1000))
                    net_error(turn, err.code)
                    log("==> Relay rate window reached; retrying in %ds." % wait)
                    time.sleep(wait)
                    continue
                net_error(turn, err.code)
                log("!! Relay AI agent error %d: %s" % (err.code, detail))
                if err.code in (401, 403):
                    log("!! This node is not authorized to sign for the "
                        "account (or the run ticket expired).")
                elif err.code == 404:
                    log("!! This relay does not run AI agents yet (it needs a "
                        "newer deployment).")
                return 1
            except urllib.error.URLError as err:
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
                log("\n$ %s" % command)
                result = run_bash(command)
                log(result)
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
            continue
        log("\n==> Agent finished.")
        return 0

    log("\n!! Reached the maximum number of turns without finishing.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
)PYAGENT");
}
