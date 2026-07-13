# Contributing to ForkMesh

Thanks for helping build ForkMesh — a peer-to-peer developer platform where
every developer runs a node and the mesh keeps serving code even when the
source machine goes offline. This is early, and that's the point: the people who
show up now help shape the protocol. Human and AI contributors are both
first-class here.

This guide covers how the project is laid out, how to build and test each
component, and how to open issues and pull requests. For higher-level context,
read the [README](README.md); for the protocol spec and per-project build notes,
see [forkmesh.com/docs](https://forkmesh.com/docs).

---

## Ways to contribute

- **Run a node** and mirror a repository you care about — every mirror makes the
  mesh more durable.
- **Fix a bug or ship a feature** in any of the components below.
- **Improve docs** — the README, this guide, and the website docs.
- **Report or triage issues.** ForkMesh dogfoods itself: issues and pull
  requests live in-repo under [`.forkmesh/`](.forkmesh/) as signed,
  append-only data, and also appear on the website.

Not sure where to start? Open an issue describing what you'd like to work on,
or pick up a funded issue and hand it to an agent.

---

## Repository layout

```text
forkmesh/
  qt_client/          Qt 6 desktop node
  cloudflare_worker/  Python Worker relay + public website
  flutter_app/        Mobile app
  ide_extension/      Editor integration for ForkMesh agents
  tools/              Standalone helpers (MCP server, PR review)
  .forkmesh/          In-repo signed data: issues (the live roadmap),
                      discussions, commit comments, release metadata, workflows
```

Work in the component your change touches. Most changes are self-contained to
one directory.

---

## Building and testing

Please build and test the component you changed before opening a pull request.

### Desktop node (`qt_client/`)

Requirements: Qt 6.4+ (Widgets, Network), CMake 3.16+, OpenSSL dev headers, Git,
a C++17 compiler, and the `openssl` CLI at runtime for the LAN TLS certificate.

```sh
cd qt_client
./run.sh          # build and run
./run.sh test     # headless backend smoke test
```

Additional test binaries live in `qt_client/tests/` and are built via CMake.

### Cloudflare relay + website (`cloudflare_worker/`)

The relay hosts encrypted room WebSockets and the signed repository catalog. It
runs on Cloudflare's Python Worker tooling (`pywrangler`) — there are no
npm/TypeScript project dependencies.

```sh
cd cloudflare_worker
uvx --from workers-py pywrangler dev      # run locally
python3 tests/test_status_page.py         # run a single test module
```

Tests live in `cloudflare_worker/tests/`. Run the module(s) relevant to your
change; if you edit a JavaScript dashboard fragment, rebuild the dashboard
bundle so the packaged output stays in sync.

### Mobile app (`flutter_app/`)

```sh
cd flutter_app
flutter pub get
flutter test
flutter analyze
```

### IDE extension (`ide_extension/`)

```sh
cd ide_extension
npm install
npm run compile   # tsc -p ./
```

### Tools (`tools/`)

```sh
python3 tools/test_forkmesh_mcp_server.py     # MCP server end-to-end
python3 tools/test_branch_pr_review_guard.py  # PR review guard
```

---

## Coding conventions

- **Match the surrounding code.** Follow the existing naming, formatting, comment
  density, and idioms of the file you're editing rather than introducing a new
  style.
- **Keep changes focused.** Make the smallest change that solves the problem;
  avoid unrelated refactors in the same pull request.
- **Respect signing and identity.** Actions that write protocol data (issues,
  pull requests, repository/profile metadata) are signed with a node's Ed25519
  key. Don't add side doors that bypass signing — write the same native
  `.forkmesh/` entries the desktop node produces.

---

## Opening an issue

**Do not report security vulnerabilities in public issues** — ForkMesh issues
are signed and published in-repo, so a public report discloses the problem
before a fix exists. Follow the private process in [SECURITY.md](SECURITY.md)
instead.

For everything else, open an issue that includes:

- what you observed and what you expected,
- the affected component (desktop node, relay, mobile, IDE extension, tools),
- steps to reproduce, and
- the version or commit you tested against.

You can file issues from the desktop node, the website, or the MCP server
(`create_issue`) — they all produce the same signed in-repo record.

---

## Opening a pull request

1. Branch from `main`.
2. Implement your change, consistent with the surrounding code.
3. If `main` has advanced, rebase or merge it into your branch and resolve
   conflicts.
4. Build and run the tests for the component you touched, and fix any failures.
5. Commit with a clear, descriptive message.
6. Open the pull request against `main`, describing what changed and how to
   verify it.

Keep pull requests scoped to a single concern so they're easy to review.

### AI agents

ForkMesh treats agents as first-class contributors. Agent-authored commits and
pull requests should be attributable — signed with the agent's key — and the
review still runs through the same human gate before merge. Whether you're a
person or an agent, the checklist above is the same.

---

## Code of conduct

Be respectful and constructive. We're building a network people want to be part
of; assume good faith, keep discussions technical, and help newcomers get their
first node on the mesh.

---

Thanks for contributing. Build a node, mirror a project you love, open an issue,
and hand it to an agent — the mesh is small enough that you'll matter, and far
enough along that you'll ship something real today.
