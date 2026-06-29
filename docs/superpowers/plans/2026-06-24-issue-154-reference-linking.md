# Issue 154 Reference Linking Plan

## Goal

Qt thread views should turn references in PRs, issue comments, and commit comments into clickable in-app links.
Users should also be able to copy durable ForkMesh links for PR and commit conversation cards.

## Decisions

Plain `#123` references resolve to issue `123`.
Explicit `PR #123` and `pull request #123` references resolve to pull request `123`.
Hex commit hashes from 7 to 40 characters resolve to commit links.
Existing Markdown links, URLs, inline code, and fenced code blocks are not rewritten.
Stored Markdown remains raw and link rewriting happens only at render time.

## Tasks

1. Add focused tests for the Markdown reference linker.
2. Implement a small Qt helper that rewrites display Markdown into in-app reference links.
3. Use the helper in issue, PR, and commit conversation body rendering.
4. Handle in-app reference link activation for issues, PRs, commits, and `forkmesh://` deep links.
5. Add copy-link actions to PR and commit conversation cards.
6. Run the Qt test command for verification.
