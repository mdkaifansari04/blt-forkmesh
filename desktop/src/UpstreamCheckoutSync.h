#pragma once

#include <QString>
#include <QStringList>

namespace forkmesh::upstream {

bool configureForkCheckoutRemote(const QString &checkoutPath,
                                 const QString &sourceUrl,
                                 const QString &forkMirrorPath,
                                 QString *error = nullptr);

struct RefreshOutcome {
    bool fetchOk = false;
    int branchesUpdated = 0; // fast-forwarded, created, or force-tracked
    int branchesPruned = 0;  // deleted: gone upstream and fully contained
    int branchesKept = 0;    // diverged local heads deliberately untouched
    bool headFastForwarded = false; // the primary checkout's branch moved
    QString error;                  // first fatal failure; empty on success

    QString summary() const;
};

RefreshOutcome refreshManagedCheckoutFromUpstream(
    const QString &checkoutPath, const QString &upstreamUrl,
    const QStringList &forcedBranches = {QStringLiteral("forkmesh/pulls")});

RefreshOutcome convergeSourceCheckoutFromMesh(const QString &checkoutPath,
                                              const QString &meshUrl);

} // namespace forkmesh::upstream
