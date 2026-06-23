# ForkMesh for VS Code / Codeium

Brings ForkMesh's issue tracker into the editor and lets you hand any issue to
**Claude Code** or **Codex** with one click. Works in VS Code and the
VS Code–based IDEs (Windsurf, Cursor, Codeium, VSCodium) via the standard
extension API.

## What it does

- Adds a **ForkMesh** icon to the activity bar (left rail).
- Lists the repository's **open issues**, read straight from the file-based
  tracker in `issues/` (the same files the ForkMesh desktop app reads/writes).
- Each issue has two inline buttons:
  - **Start with Claude Code** — opens a terminal at the repo root and runs
    Claude Code seeded with a prompt built from the issue.
  - **Start with Codex** — same, for Codex.
- Integrates with the **ForkMesh desktop app**: when you enable IDE integration
  there, its issue view gains "run in IDE" buttons that drive this extension.

## Requirements

- A workspace folder that is a ForkMesh repository (contains an `issues/`
  directory). Override the location with `forkmesh.issuesPath`.
- The `claude` and/or `codex` CLIs on your `PATH` (or customize the commands
  below).

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `forkmesh.claudeCommand` | `claude "$(cat {promptFile})"` | Command to start Claude Code. `{promptFile}` → generated prompt file, `{issueNumber}` → issue number. |
| `forkmesh.codexCommand` | `codex "$(cat {promptFile})"` | Command to start Codex. |
| `forkmesh.issuesPath` | _(auto)_ | Absolute path to an `issues/` dir; empty = auto-detect `<workspace>/issues`. |
| `forkmesh.showClosedIssues` | `false` | Also list closed issues. |

> The default commands use `$(cat …)` (a POSIX shell). On Windows, set a
> PowerShell-friendly command, e.g. `claude (Get-Content {promptFile} -Raw)`.

## Desktop-app handshake

While running, the extension maintains a heartbeat file and watches for task
requests under `~/.forkmesh/ide/` — see [PROTOCOL.md](./PROTOCOL.md). The
ForkMesh desktop app uses these to detect the extension and to launch issues in
the IDE. No network sockets are involved.

## Develop / build

```sh
cd ide_extension
npm install
npm run compile          # or: npm run watch
# Press F5 in VS Code to launch an Extension Development Host.
npm run package          # produces forkmesh-<version>.vsix (needs @vscode/vsce)
```

Install the packaged build with:

```sh
code --install-extension forkmesh-0.1.0.vsix
# Windsurf / Cursor / VSCodium expose the same CLI under their own name.
```
