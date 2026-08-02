#pragma once

#include <QString>
#include <QStringList>

// Upstream tracking for a service-managed headless checkout.
//
// Headless fleet nodes keep a real working tree (the installer-provisioned
// agent checkout) registered as the flagship repository's localPath so agent
// jobs have somewhere to run. repositorySource() therefore points the sealing
// sync at that checkout — which is correct as the *serving* source, but means
// nothing ever pulled new commits from the network again: the node served its
// install-time snapshot forever (mirror6/7/8 froze this way).
//
// refreshManagedCheckoutFromUpstream() closes that loop. Before each seal, it
// fetches the relay clone URL into the checkout and converges local refs on
// upstream without ever discarding local-only work:
//   - the branch checked out in any worktree is fast-forwarded only when that
//     worktree is clean, via its own worktree (reset for forced branches);
//   - other local heads are force-moved only when their tip is already
//     contained in the upstream tip (a pure fast-forward);
//   - heads that vanished upstream are deleted only when fully contained in
//     upstream main (nothing unique is lost); anything diverged is kept;
//   - branches named in forcedBranches (the pulls metadata branch) hard-track
//     upstream even across divergence — they are owner-authored metadata a
//     mirror must never fork.
namespace forkmesh::upstream {

struct RefreshOutcome {
    bool fetchOk = false;
    int branchesUpdated = 0; // fast-forwarded, created, or force-tracked
    int branchesPruned = 0;  // deleted: gone upstream and fully contained
    int branchesKept = 0;    // diverged local heads deliberately untouched
    bool headFastForwarded = false; // the primary checkout's branch moved
    QString error;                  // first fatal failure; empty on success

    // One bounded human-readable line for the system log; empty when the
    // refresh was a clean no-op (already current, nothing to say).
    QString summary() const;
};

// checkoutPath must be a non-bare working tree; upstreamUrl is the relay
// clone URL. Runs bounded git subprocesses; safe on a worker thread. Never
// throws; failures land in RefreshOutcome::error and leave the checkout
// serving its previous state.
RefreshOutcome refreshManagedCheckoutFromUpstream(
    const QString &checkoutPath, const QString &upstreamUrl,
    const QStringList &forcedBranches = {QStringLiteral("forkmesh/pulls")});

// The strictly-additive variant for a SOURCE-OF-TRUTH working copy converging
// on submissions an online mirror merged (and drained) while this node was
// offline. Differences from the managed-checkout refresh: the user's remote
// configuration is never touched (one-shot fetch by URL into a scratch
// remote-tracking namespace), nothing is ever pruned, and no branch is force
// tracked — every local ref moves only by fast-forward through a clean
// worktree, so local-only commits always survive and supersede the mesh on
// this node's next publish.
RefreshOutcome convergeSourceCheckoutFromMesh(const QString &checkoutPath,
                                              const QString &meshUrl);

} // namespace forkmesh::upstream
