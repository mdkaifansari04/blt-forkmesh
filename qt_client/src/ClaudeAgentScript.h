#pragma once













#include <QString>

inline QString forkmeshClaudeAgentScript()
{
    return QString::fromUtf8(R"PYAGENT(#!/usr/bin/env python3
"""Minimal Claude coding agent for ForkMesh (Anthropic Messages API, stdlib only)."""
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

API_URL = "https://api.anthropic.com/v1/messages"
API_VERSION = "2023-06-01"
MAX_TURNS = 60
TOOL_OUTPUT_LIMIT = 30000


def log(text):
    sys.stdout.write(text)
    sys.stdout.write("\n")
    sys.stdout.flush()


def read_prompt():

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


TOOLS = [
    {
        "name": "bash",
        "description": (
            "Run a shell command in the current working directory (the checked-out "
            "worktree). Use it to read, search, and edit files needed for the issue. "
            "Do not commit, push, or use the network unless the issue requires it."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "command": {
                    "type": "string",
                    "description": "The shell command to execute.",
                }
            },
            "required": ["command"],
        },
    }
]


def call_api(api_key, model, max_tokens, messages):
    body = json.dumps(
        {
            "model": model,
            "max_tokens": max_tokens,
            "tools": TOOLS,
            "messages": messages,
        }
    ).encode("utf-8")
    request = urllib.request.Request(API_URL, data=body, method="POST")
    request.add_header("content-type", "application/json")
    request.add_header("anthropic-version", API_VERSION)
    request.add_header("x-api-key", api_key)
    with urllib.request.urlopen(request, timeout=600) as response:
        return json.loads(response.read().decode("utf-8"))





def net_request(turn, model, msg_count):
    log("==> [net] \U0001F310 request #%d → POST api.anthropic.com/v1/messages "
        "(model=%s, messages=%d)" % (turn, model, msg_count))


def net_response(turn, usage, stop_reason):
    inp = int(usage.get("input_tokens") or 0)
    out = int(usage.get("output_tokens") or 0)
    log("==> [net] \U0001F310 response #%d ← in=%d out=%d stop=%s"
        % (turn, inp, out, stop_reason or "end"))


def net_error(turn, code):
    log("==> [net] \U0001F310 error #%d ← HTTP %s" % (turn, code))


def main():
    api_key = os.environ.get("ANTHROPIC_API_KEY", "").strip()
    if not api_key:
        log("!! No ANTHROPIC_API_KEY is set. Add a Claude API key in Settings.")
        return 1

    model = os.environ.get("FORKMESH_AGENT_MODEL", "").strip() or "claude-sonnet-4-6"
    try:
        max_tokens = int(os.environ.get("FORKMESH_AGENT_MAX_OUTPUT_TOKENS", "2000"))
    except ValueError:
        max_tokens = 2000
    max_tokens = max(256, max_tokens)

    prompt = read_prompt().strip()
    if not prompt:
        log("!! Empty prompt; nothing to do.")
        return 1

    log("==> Claude agent (model %s) talking to the Anthropic API directly." % model)
    messages = [{"role": "user", "content": prompt}]

    for turn in range(1, MAX_TURNS + 1):
        net_request(turn, model, len(messages))
        try:
            data = call_api(api_key, model, max_tokens, messages)
        except urllib.error.HTTPError as err:
            net_error(turn, err.code)
            detail = err.read().decode("utf-8", "replace")
            log("!! Anthropic API error %d: %s" % (err.code, detail))
            if err.code in (401, 403):
                log("!! The API key was rejected; rotate it and re-enter it in Settings.")
            return 1
        except urllib.error.URLError as err:
            net_error(turn, "unreachable")
            log("!! Could not reach the Anthropic API: %s" % err.reason)
            return 1

        net_response(turn, data.get("usage", {}), data.get("stop_reason"))
        content = data.get("content", [])
        tool_results = []
        for block in content:
            if block.get("type") == "text":
                text = block.get("text", "").strip()
                if text:
                    log("\n● %s" % text)
            elif block.get("type") == "tool_use":
                command = block.get("input", {}).get("command", "")
                log("\n$ %s" % command)
                result = run_bash(command)
                log(result)
                tool_results.append(
                    {
                        "type": "tool_result",
                        "tool_use_id": block.get("id"),
                        "content": result,
                    }
                )

        messages.append({"role": "assistant", "content": content})
        if data.get("stop_reason") == "tool_use" and tool_results:
            messages.append({"role": "user", "content": tool_results})
            continue
        log("\n==> Agent finished.")
        return 0

    log("\n!! Reached the maximum number of turns without finishing.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
)PYAGENT");
}
