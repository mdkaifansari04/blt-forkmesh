#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

class QSettings;

namespace forkmesh::mirror_actions {

using CatalogConfigurationPublisher =
    std::function<bool(const QString &refreshConfigurationPath,
                       bool actionsEnabled)>;





QJsonObject applyConfiguration(const QJsonObject &request,
                               QSettings &settings,
                               const CatalogConfigurationPublisher &publisher =
                                   CatalogConfigurationPublisher());





bool recoverPendingConfiguration(
    QSettings &settings,
    const CatalogConfigurationPublisher &publisher =
        CatalogConfigurationPublisher(),
    QString *errorCode = nullptr);




QString configurationRecoveryJournalPath(const QSettings &settings);




int runConfigurationStdin(int argc, char *argv[]);



inline constexpr auto kEnabledSetting = "actions/mirrorEnabled";
inline constexpr auto kGenerationSetting = "actions/mirrorConfigGeneration";
inline constexpr auto kNodeSetting = "actions/mirrorNode";
inline constexpr auto kSummaryPathSetting = "actions/mirrorSummaryPath";

}
