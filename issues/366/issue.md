---
schema: forkmesh-issue-v1
number: 366
title: Expose the mesh as an MCP server (repos, issues, PRs as tools)
status: closed
labels: [feature]
milestone: v1
priority: 41
progress: 0
assignees: [Claude Code]
createdAt: 1783116818265
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-366
ts: 1783116818265
attachments: []
sig: zHqqrljY-j-INqYWYz9kIveA1mtDoUMwH5LeMl-jE5OiO1Y1PN0jf_rDah7TZLpWxaDBL3VSXoSHKo_7JaNIAA
---

**Roadmap Phase 5.** Any agent tooling — not just the built-in Claude Code/Codex integrations — should be able to work the mesh. MCP is the de-facto integration surface in 2026.

- The desktop node already speaks hand-rolled JSON-RPC over WebSocket for the Claude IDE bridge (`qt_client/src/ClaudeIdeBridge.cpp`, no Qt6::WebSockets dependency); MCP is JSON-RPC too, so most plumbing is reusable. stdio transport is fine for local-first.
- Tools to expose: `list_repos`, `read_file`, `search_issues`, `create_issue`, `comment_on_issue`, `open_pr_from_branch`, `get_pr_diff`. Write-tools sign with the node identity key and go through the exact same paths as the UI (no privileged side door).
- Register via a `.mcp.json` entry so Claude Code picks it up automatically.

Spec: <https://modelcontextprotocol.io/specification>.
