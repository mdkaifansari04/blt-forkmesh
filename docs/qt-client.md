# Qt desktop client

The Qt client is both the ForkMesh desktop application and a node: it provides
chat and repository UI, keeps local Git mirrors, signs collaboration records,
serves eligible repositories, runs actions, and can host coding agents.

## Install a release

For normal use, install the published binary:

```sh
curl -fsSL https://forkmesh.com/install.sh | bash
```

The installer verifies the release checksum and creates a desktop launcher. To
develop the client, build it from source instead.

## Build and run from source

Requirements:

- Qt 6.4+ components: Widgets, Network, SVG, Concurrent, and D-Bus on Linux.
- CMake 3.16+ and a C++17 compiler.
- OpenSSL development headers and the `openssl` command.
- Git.

Common dependency commands:

```sh
# Ubuntu / Debian
sudo apt update
sudo apt install build-essential cmake git libssl-dev openssl \
  qt6-base-dev qt6-svg-dev

# Fedora
sudo dnf install cmake gcc-c++ git openssl openssl-devel \
  qt6-qtbase-devel qt6-qtsvg-devel

# Arch Linux
sudo pacman -S --needed base-devel cmake git openssl qt6-base qt6-svg

# macOS (after installing Xcode Command Line Tools and Homebrew)
brew install cmake git openssl@3 qt
```

From the repository root:

```sh
cd qt_client
./run.sh
```

`run.sh` configures a Release build, builds incrementally using all available
cores, and launches the correct Linux or macOS binary. It also detects a CMake
cache copied from another path and safely regenerates it.

Available commands:

```sh
./run.sh run       # incremental build and launch; "run" is optional
./run.sh test      # build and run the main headless/window test suites
./run.sh clean     # delete qt_client/build
./run.sh rebuild   # clean, build, and launch
```

Manual CMake commands are useful for IDEs and focused tests:

```sh
cmake -S qt_client -B qt_client/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DFORKMESH_BUILD_TESTS=ON
cmake --build qt_client/build --parallel
ctest --test-dir qt_client/build --output-on-failure
```

If CMake cannot find Qt on macOS, pass Homebrew's prefix:

```sh
cmake -S qt_client -B qt_client/build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
```

Never run ForkMesh as root. The node runs Git commands and repository workflows
with the current user's permissions.

## First run and navigation

A first launch creates the node identity, connects to the default mainnode, and
starts syncing the ForkMesh project. The main areas are:

- **Chat** — direct conversations, repository channels, and mirrored World
  office channels. Compose in the input at the bottom. Office replies require a
  signed-in account so the client can obtain scoped encrypted-room access.
- **Repos / Network** — create or add local repositories, browse the mesh, fork
  a project, and control public/private visibility.
- **Code** — browse and edit files, view changes, commit, inspect branches and
  worktrees, and create a PR from a branch.
- **Issues / Pulls / Agents / Actions** — signed collaboration records,
  reviews, isolated agent worktrees, and workflow runs.
- **Network** — the relays, nodes, and hosts this client knows about, each as a
  tab with its own count, alongside live HTTP/WebSocket state and recent request
  metadata. The rail's Network badge shows the three counts added together.
- **Settings** — account, node, agent, notification, network, storage, and
  backup controls.

To add an existing project, choose **Repos → Add local repo** and select the
working-tree folder. To contribute to someone else's repository, find it under
**Repos**, choose **Fork**, and select a parent folder for the new working copy.

## Create a pull request

For a PR within a repository you own:

1. Open the writable repository and choose **Code → Branches → New branch**.
2. Make the change.
3. Choose **Code → Changes**, stage the files, enter a message, and commit.
4. Choose **Code → Branches**, select the branch, review its diff, and click
   **Create PR**.
5. Enter the title. ForkMesh records the commit series and a signed binary-safe
   patch, then opens the new PR.

The button is disabled when the branch is the default branch, has no diff, or
the repository has no writable working tree. Agents follow the same model: each
agent works in an isolated worktree and can produce a branch-backed PR.

For a PR to another owner:

1. Find the upstream repository under **Repos**, choose **Fork**, and select a
   folder for your writable checkout.
2. Create and commit the feature branch in that fork.
3. Open **Pulls → New pull request**.
4. Set **Target node** to the upstream owner, **Base** to its default branch
   (normally `main`), and **Head** to your feature branch.
5. Add the title and description, then submit. ForkMesh signs the patch and
   commit series and delivers it to the target node's inbox. The relay can hold
   it until the owner comes online.

For a very small one-file fix, a bare mirror offers a shortcut: edit the file
in ForkMesh and choose **Save as PR**. The client builds the signed commit in a
temporary worktree and sends it directly to the repository owner's inbox.

**Send to source of truth** is a recovery/re-delivery action shown while viewing
an open PR on a read-only mirror; it is not needed for the normal fork workflow.

For project-specific tests and the required PR evidence, see
[Contributing](../CONTRIBUTING.md).

## Run as a headless node

On a Linux server with no display, headless mode is selected automatically. You
can also request it explicitly:

```sh
./qt_client/build/forkmesh --headless
```

Type `help` for commands such as `status`, `peers`, `repos`, `mirrors`, `sync`,
and `setup <node-name>`. Closing stdin does not stop the node; use `quit`,
Ctrl-C, SIGTERM, or the service manager to stop it.

Operational deployments should follow the dedicated
[headless mirror runbook](operations/headless-mirror-refresh.md), not an
ad-hoc shell background process.

## Logs and diagnostics

Start in the UI:

- Click the CPU/RAM/stall indicator in the footer for UI-stall details.
- Open **Network diagnostics** for current connections and recent endpoint
  results.
- Enable **Settings → Log every network request in the network log** only while
  diagnosing a problem; it is intentionally opt-in.
- Open **Settings → Data** to see and open the exact application-data,
  preferences, mirror, and cache directories for the current OS.

Durable diagnostic journals are stored separately:

```text
~/.forkmesh/diagnostics/actions.jsonl   bounded action lifecycle journal
~/.forkmesh/diagnostics/stalls.log      UI freezes longer than 500 ms
~/.forkmesh/diagnostics/stalls.log.1    previous rotated stall journal
```

The application data directory also contains the persistent network log and
crash records. On Linux it is normally
`~/.local/share/ForkMesh/ForkMesh/`; use **Settings → Data** instead of assuming
that path on macOS or Windows.

When reporting a problem, include:

1. `./qt_client/build/forkmesh --version` output.
2. The operating system and display server (Wayland/X11 when relevant).
3. The exact action that triggered the problem.
4. A short relevant log excerpt with secrets, tokens, private repository names,
   and private message contents removed.

## Common fixes

- **CMake cannot find Qt6:** install the Qt development packages, not only the
  runtime; on macOS pass `CMAKE_PREFIX_PATH` as shown above.
- **Qt platform plugin error:** launch from a graphical session, or use
  `--headless` on a server. Linux screenshot capture under Wayland also needs
  the desktop portal and Qt D-Bus support.
- **A moved checkout reports a stale CMake cache:** `run.sh` fixes this
  automatically; otherwise run `./run.sh clean`.
- **A normal build is unexpectedly slow:** use the incremental `./run.sh`;
  install `ccache` to let objects survive clean builds and branch changes.
- **Chat will not send:** select a conversation and confirm the node is
  connected. World office replies also require a signed-in account; reconnect
  after signing in so the client can obtain room access.
- **Repository actions are read-only:** fork the repository or add its local
  working tree.
- **Need a safe backup:** use **Settings → Data → Export configuration** before
  deleting or importing anything.

Back to [Getting started](getting-started.md) or the
[documentation index](README.md).
