# ForkMesh documentation

Start with the guide that matches what you are trying to do:

| Goal | Guide |
| --- | --- |
| Install ForkMesh and complete the first-run setup | [Getting started](getting-started.md) |
| Build, run, and use the desktop application | [Qt client guide](qt-client.md) |
| Change the project and open a pull request | [Contributing](../CONTRIBUTING.md) |
| Understand what “done” requires | [Definition of done](engineering/definition-of-done.md) |
| Import repositories from another forge | [Repository imports](repository-imports.md) |
| Operate a desktop control node | [Desktop control node](operations/desktop-control-node.md) |

The hosted documentation at [forkmesh.com/docs](https://forkmesh.com/docs)
contains the complete protocol reference, release notes, and build notes for
every client. Files in this directory cover repository-specific development,
security, design, and operations.

## Reference by area

- `design/` — protocol and federation design decisions.
- `engineering/` — quality gates, performance budgets, and completion policy.
- `operations/` — deployment, recovery, gateways, headless nodes, and service
  runbooks.
- `security/` — security boundaries and local integration contracts.
- `*.schema.json` — machine-readable configuration and protocol schemas.

If the application is not behaving as expected, start with
[Qt client diagnostics](qt-client.md#logs-and-diagnostics). Include the version,
the action you took, and the relevant log excerpt when opening an issue.
