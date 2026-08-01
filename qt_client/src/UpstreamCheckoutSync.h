#pragma once

#include <QString>
#include <QStringList>






















namespace forkmesh::upstream {

struct RefreshOutcome {
    bool fetchOk = false;
    int branchesUpdated = 0;
    int branchesPruned = 0;
    int branchesKept = 0;
    bool headFastForwarded = false;
    QString error;



    QString summary() const;
};





RefreshOutcome refreshManagedCheckoutFromUpstream(
    const QString &checkoutPath, const QString &upstreamUrl,
    const QStringList &forcedBranches = {QStringLiteral("forkmesh/pulls")});









RefreshOutcome convergeSourceCheckoutFromMesh(const QString &checkoutPath,
                                              const QString &meshUrl);

}
