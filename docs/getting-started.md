# Getting started with ForkMesh

This guide takes you from a new machine to a running desktop node, a repository
on the mesh, working chat, and your first change.

## Choose how to install

The fastest path is the published, SHA-256-verified installer:

```sh
curl -fsSL https://forkmesh.com/install.sh | bash
```

If you want to change ForkMesh itself, clone the source and run the Qt client:

```sh
git clone https://forkmesh.com/forkmesh/forkmesh
cd forkmesh/qt_client
./run.sh
```

The source build needs Qt 6.4 or newer with Widgets, Network, SVG, Concurrent,
and, on Linux, D-Bus; CMake 3.16 or newer; OpenSSL development headers; Git; a
C++17 compiler; and the `openssl` command. Exact package commands and manual
build instructions are in the [Qt client guide](qt-client.md).

## Your first five minutes

1. Launch ForkMesh. A local Ed25519 node identity and a fun temporary node name
   are created automatically. The client connects to the default mainnode; no
   server URL is required.
2. Sign in or create an account from **Settings** if you want a portable public
   username. The node identity remains on this device.
3. Wait for the initial ForkMesh project mirror to finish syncing. A fresh
   install opens it automatically.
4. Open **Chat**, choose a conversation or repository channel, type in the
   composer at the bottom, and press Enter. Signed-in users can also reply in
   mirrored World office channels; the client obtains the scoped room access
   and sends the message over its encrypted office socket.
5. Open **Repos** and choose one of these paths:

   - **New repository** creates a Git repository, optional README, local mirror,
     and signed catalog entry.
   - **Add local repo** selects an existing local `git init`/`git clone`
     directory and asks whether it is public or private.
   - **Repos → Fork** makes an independent writable copy of a repository you
     found on the mesh.

Public repositories appear in the network catalog. Private repositories remain
hidden from public browse and clone routes unless you explicitly share them.
ForkMesh publishes signed metadata; it does not publish a local filesystem path.

## Make a change

You can use normal Git commands in the repository folder:

```sh
git switch -c docs/my-change
# edit files
git add -A
git commit -m "docs: explain my change"
```

Or stay in the desktop application:

1. Open the repository, then **Code → Branches → New branch**.
2. Edit files in your editor or ForkMesh's file view.
3. Open **Code → Changes**, enter a commit message, then choose **Commit** or
   **Commit & push**.
4. For a PR within a repository you own, return to **Code → Branches**, select
   the feature branch, and choose **Create PR**.

For an upstream project, fork it first. In the writable fork, open
**Pulls → New pull request**, select the upstream owner as **Target node**, and
choose `main` as the base and your feature branch as the head. That sends the
signed PR to the owner's inbox; the relay queues it even when the owning node is
offline.
See [Contributing](../CONTRIBUTING.md#opening-a-pull-request) for the complete
review checklist.

## Where ForkMesh stores data

Use **Settings → Data** for the authoritative paths on your machine. That page
can open each directory and export or restore configuration data.

Typical Linux locations are:

```text
~/.local/share/ForkMesh/ForkMesh/   identity, chat, agents, mirrors, local stores
~/.config/ForkMesh/ForkMesh.conf    preferences
~/.cache/ForkMesh/ForkMesh/         disposable cache
~/.forkmesh/diagnostics/             action and UI-stall journals
```

Do not delete application data casually: it contains the private identity key
for this node. Prefer **Settings → Data → Export configuration** before moving
machines or testing recovery.

## If something does not work

- Build failure: check the dependency and clean-build sections in the
  [Qt client guide](qt-client.md#build-and-run-from-source).
- Cannot type or send in chat: select a conversation first. World office replies
  also require a signed-in account on this node; reconnect after signing in so
  the client can obtain room access.
- Repository is read-only: use **Fork** to create a local working copy.
- A branch has no **Create PR** action: it must differ from the default branch
  and the repository must have a writable working tree. For an upstream PR, use
  **Pulls → New pull request** and select the upstream target node.
- Network or UI problem: use the built-in diagnostics and logs described in
  [Qt client diagnostics](qt-client.md#logs-and-diagnostics).

Next: [learn the Qt client](qt-client.md), [open a pull
request](../CONTRIBUTING.md#opening-a-pull-request), or browse the
[documentation index](README.md).
