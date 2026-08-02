#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

class QSettings;

namespace forkmesh::mirror_actions {

using CatalogConfigurationPublisher =
    std::function<bool(const QString &refreshConfigurationPath,
                       bool actionsEnabled)>;

// Apply one controller request to the device-local Actions configuration.
// The returned object is deliberately metadata-only: variable names and values
// never appear in it. The caller encodes it as the fixed result sentinel used
// by the SSH controller.
QJsonObject applyConfiguration(const QJsonObject &request,
                               QSettings &settings,
                               const CatalogConfigurationPublisher &publisher =
                                   CatalogConfigurationPublisher());

// Reconcile an interrupted catalog/settings transaction. Recovery restores and
// durably verifies the exact previous local configuration before rolling the
// catalog back, then removes the owner-only journal. It is safe and idempotent
// at process startup and before a new configuration request.
bool recoverPendingConfiguration(
    QSettings &settings,
    const CatalogConfigurationPublisher &publisher =
        CatalogConfigurationPublisher(),
    QString *errorCode = nullptr);

// Exposed for diagnostics and crash-recovery tests. The journal contains only
// transaction metadata plus a protected snapshot of values already stored on
// this device; replacement values are never written to it before publication.
QString configurationRecoveryJournalPath(const QSettings &settings);

// Read, validate, and apply one bounded JSON request from stdin. This is a
// short-lived helper mode used by `forkmesh --configure-mirror-actions-stdin`.
// It writes exactly one base64url result sentinel and no request contents.
int runConfigurationStdin(int argc, char *argv[]);

// Keys shared with the long-running MainWindow process. It polls the generation
// and applies a remotely changed state without a restart.
inline constexpr auto kEnabledSetting = "actions/mirrorEnabled";
inline constexpr auto kGenerationSetting = "actions/mirrorConfigGeneration";
inline constexpr auto kNodeSetting = "actions/mirrorNode";
inline constexpr auto kSummaryPathSetting = "actions/mirrorSummaryPath";

} // namespace forkmesh::mirror_actions
