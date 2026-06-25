# Agent Workflow delte this

For issue work in a treehouse, fetch the latest main changes first, then create a feature branch from that refreshed main inside the treehouse.
Read the issue, inspect the relevant code, make a short plan, then delegate the focused implementation to a fresh Codex worker agent.
After the worker reports back, the controller reviews the diff, fixes gaps if needed, runs verification, and reports the result briefly.
When asked to commit and make a ForkMesh PR, commit only intended files with a conventional message, then create a signed patch PR by diffing `main..HEAD`, signing the PullStore-compatible canonical string with the local ForkMesh Ed25519 identity, and POSTing to `/api/repo/{owner}/{repo}/pulls`.
Do not treat a branch push as a ForkMesh PR.
