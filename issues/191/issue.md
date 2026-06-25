---
schema: forkmesh-issue-v1
number: 191
title: Native Claude Code IDE integration: have the app act as the IDE the claude CLI connects to (in-app diffs, selection, open-file context)
status: closed
labels: []
milestone: 
priority: 1
progress: 0
assignees: []
createdAt: 1782335051280
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-191
ts: 1782335051280
attachments: []
sig: YeGjvTdg7OXZaLWl5fotGCMkJJpFsKtiLU2h7zJePVEWddStLRFpyXe7F0NsGqbFdqakqijZhfll_C9nutI0Aw
---

## Goal

Bring the Claude Code VS Code extension's *editor-integration* features into the
Qt desktop app **without embedding VS Code**. A `.vsix` can't be loaded into a
native Qt app, and Anthropic's extension is really just a thin UI shell over the
local `claude` CLI: it discovers a running `claude` process and talks to it over
a WebSocket + JSON-RPC (MCP variant), coordinated by a lockfile at
`~/.claude/ide/<port>.lock`. So instead of hosting the extension, ForkMesh
should *be* the IDE that `claude` connects to.

## Approach (implement the IDE-integration protocol natively)

1. **WebSocket + JSON-RPC server** — new `ClaudeIdeBridge` class using
   `Qt6::WebSockets` (add to qt_client/CMakeLists.txt). Bind 127.0.0.1 on an
   ephemeral port; speak JSON-RPC 2.0 (the MCP variant); validate the auth token
   from the lockfile on handshake.
2. **Lockfile + env injection** — write `~/.claude/ide/<port>.lock` (port,
   workspace folders, IDE name, transport, random auth token) and inject the env
   vars the CLI looks for when launching `claude` in TerminalWidget. Then the CLI
   auto-connects (or the user runs `/ide`).
3. **IDE-side tool handlers**, wired to existing widgets:
   - `openDiff` (the high-value one: inline accept/reject of proposed changes)
   - `getCurrentSelection` / `getLatestSelection`
   - `getOpenEditors` / `getWorkspaceFolders`
   - `openFile`, `closeAllDiffTabs`, `getDiagnostics`
4. **Notifications** — push `selection_changed` (and `at_mentioned` for "send
   selection to Claude") from the editor up to the CLI.

## Reference implementation

coder/claudecode.nvim is a clean-room implementation of this exact protocol and
is authoritative for the lockfile JSON shape, env var names, the auth header, and
each tool's request/response schema. Port its `lockfile`, `server`, and `tools`
modules to C++. Docs: https://code.claude.com/docs/en/vs-code

## Why this fits ForkMesh

The app already runs `claude` in a forkpty terminal (TerminalWidget) and has an
AgentRunner; this adds the editor *surface* the CLI talks back to — no Electron,
no Node, no marketplace licensing. Rough effort: the WS + lockfile + env plumbing
is ~1-2 days; the real value is how rich `openDiff` and selection sync are made.

## Out of scope

Embedding a full VS Code surface (code-server / Theia / VSCodium) — rejected for
the same reasons the xterm `-into` embedding was dropped for a custom terminal.
