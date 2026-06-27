---
schema: forkmesh-pull-v1
number: 37
title: issue #296: show and store the agent run summary (turns / duration / cost) in the list
base: main
head: agent/issue-296-on-the-agent-list-when-the-agent-is-finished-it
status: open
ts: 1782520250612
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
sig: LggRaMNaww09HYhU_BoEXOeQ3tSkbTp1HEck06LYKp31XX2dko9enwfPh9IAaLnLmRcNpHz7sIuFsUaoUZQjAg
---

When a Claude Code agent finishes, the transcript shows a summary line like "✓ done · 71 turns · 828s · $4.59", but the agents **list** never captured those numbers — the Cost column read $0 for Claude Code runs and turns/duration were lost on restart (issue #296).

This captures the CLI's final `result` event (`num_turns`, `duration_ms`, `total_cost_usd`) onto the AgentSession and persists it, so the summary now shows in the list and survives an app restart:

- new `numTurns` / `durationMs` fields on `AgentSession`, serialized in `AgentStore` (the real `total_cost_usd` is stored into the existing `costUsd`)
- the Status cell now reads e.g. `success · 71 turns · 828s`, alongside the Cost column's real `$X`
- cells refresh in place (new `updateAgentCostCell`, reused `updateAgentStatusCell`) so the open transcript isn't rebuilt
- test covers the turns/duration/cost round-trip through a store reopen
