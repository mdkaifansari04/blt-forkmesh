#include "MirrorActionsConfiguration.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

#include <cstdio>

namespace forkmesh::mirror_actions {
namespace {

constexpr qsizetype kMaximumRequestBytes = 64 * 1024;
constexpr qsizetype kMaximumVariableValueBytes = 16 * 1024;
constexpr qsizetype kMaximumVariableBytes = 48 * 1024;
constexpr int kMaximumVariables = 64;

const QRegularExpression kRequestId(
    QStringLiteral("^[a-f0-9]{32}$"));
const QRegularExpression kNodeName(
    QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
const QRegularExpression kRepositoryName(
    QStringLiteral("^[A-Za-z0-9._-]{1,100}$"));
const QRegularExpression kBranchName(
    QStringLiteral("^(?!/)(?!.*(?:\\.\\.|//))[A-Za-z0-9._/-]{1,120}(?<!/)$"));
const QRegularExpression kVariableName(
    QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,63}$"));

QJsonObject result(const QString &requestId, const QString &node,
                   bool enabled, bool replaced, int count, bool ok,
                   const QString &errorCode = {})
{
    QJsonObject value{
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.mirror-actions-configuration-result")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("ok"), ok},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("node"), node},
        {QStringLiteral("actionsEnabled"), enabled},
        {QStringLiteral("variablesReplaced"), replaced},
        {QStringLiteral("variableCount"), count},
    };
    if (!errorCode.isEmpty())
        value.insert(QStringLiteral("errorCode"), errorCode);
    return value;
}

bool exactFields(const QJsonObject &object, const QSet<QString> &required,
                 const QSet<QString> &optional = {})
{
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());
    const QSet<QString> missing = required - actual;
    QSet<QString> allowed = required;
    allowed.unite(optional);
    const QSet<QString> unexpected = actual - allowed;
    return missing.isEmpty() && unexpected.isEmpty();
}

QString safeNode(const QJsonObject &request)
{
    const QString value =
        request.value(QStringLiteral("node")).toString().trimmed();
    return kNodeName.match(value).hasMatch() ? value : QString();
}

QString safeRequestId(const QJsonObject &request)
{
    const QString value =
        request.value(QStringLiteral("requestId")).toString().trimmed();
    return kRequestId.match(value).hasMatch()
               ? value
               : QString(32, QLatin1Char('0'));
}

bool safeAbsolutePath(const QString &value)
{
    if (value.size() < 2 || value.size() > 4096 ||
        value.contains(QChar::Null) || value.contains(QLatin1Char('\r')) ||
        value.contains(QLatin1Char('\n'))) {
        return false;
    }
    const QFileInfo info(value);
    return info.isAbsolute() && !info.isSymLink();
}

QJsonObject readGatewayConfiguration(QString *pathOut)
{
    QStringList candidates;
    const QString configured =
        qEnvironmentVariable("FORKMESH_MIRROR_REFRESH_CONFIG").trimmed();
    if (!configured.isEmpty())
        candidates.append(configured);
    candidates.append(
        QStringLiteral(
            "/var/lib/forkmesh-mirror/gateway/mirror-refresh.json"));
    for (const QString &path : candidates) {
        if (!safeAbsolutePath(path))
            continue;
        QFileInfo info(path);
        if (!info.isFile() || info.isSymLink() || info.size() <= 0 ||
            info.size() > 64 * 1024)
            continue;
        const QFileDevice::Permissions permissions = info.permissions();
        if (permissions.testFlag(QFileDevice::WriteGroup) ||
            permissions.testFlag(QFileDevice::WriteOther)) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject())
            continue;
        const QJsonObject value = document.object();
        if (value.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
            value.value(QStringLiteral("type")).toString() !=
                QLatin1String("forkmesh.headless-mirror-refresh"))
            continue;
        if (pathOut)
            *pathOut = path;
        return value;
    }
    return {};
}

QString repositoryOwner(const QJsonObject &gateway)
{
    const QString node =
        gateway.value(QStringLiteral("nodeOwner")).toString().trimmed();
    const QJsonArray aliases =
        gateway.value(QStringLiteral("ownerAliases")).toArray();
    for (const QJsonValue &value : aliases) {
        const QString alias = value.toString().trimmed();
        if (alias != node && kNodeName.match(alias).hasMatch())
            return alias;
    }
    return node;
}

QString actionMirrorPath(QSettings &settings, const QString &node,
                         const QString &owner,
                         const QString &repository)
{
    const QByteArray key =
        QCryptographicHash::hash(
            (node + QLatin1Char('/') + owner + QLatin1Char('/') + repository)
                .toUtf8(),
            QCryptographicHash::Sha256)
            .toHex()
            .left(24);
    const QString dataRoot =
        settings.format() == QSettings::IniFormat
            ? QDir(QFileInfo(settings.fileName()).absolutePath())
                  .filePath(QStringLiteral("forkmesh-actions-test-data"))
            : QStandardPaths::writableLocation(
                  QStandardPaths::AppDataLocation);
    return QDir(dataRoot)
        .filePath(QStringLiteral("actions/mirrors/") +
                  QString::fromLatin1(key) + QStringLiteral(".git"));
}

bool runGit(const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForStarted(5000) ||
        !process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

bool prepareActionMirror(const QString &source, const QString &destination)
{
    if (!safeAbsolutePath(source) || !QFileInfo(source).isDir() ||
        QFileInfo(source).isSymLink() || !safeAbsolutePath(destination))
        return false;
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        return false;
    if (!QFileInfo(destination).exists()) {
        return runGit(
            {QStringLiteral("clone"), QStringLiteral("--mirror"),
             QStringLiteral("--no-hardlinks"), source, destination},
            30000);
    }
    if (!QFileInfo(QDir(destination).filePath(QStringLiteral("HEAD"))).isFile())
        return false;
    return runGit(
        {QStringLiteral("--git-dir"), destination, QStringLiteral("fetch"),
         QStringLiteral("--prune"), source,
         QStringLiteral("+refs/heads/*:refs/heads/*"),
         QStringLiteral("+refs/tags/*:refs/tags/*")},
        30000);
}

bool upsertRepository(QSettings &settings, const QString &owner,
                      const QString &repository, const QString &source,
                      const QString &mirror, const QString &branch,
                      bool enabled)
{
    constexpr auto repositoriesKey = "repositories/items";
    QList<QVariantMap> records;
    const int count = settings.beginReadArray(
        QString::fromLatin1(repositoriesKey));
    records.reserve(qMax(0, count) + 1);
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        QVariantMap record;
        const QStringList keys = settings.childKeys();
        for (const QString &key : keys)
            record.insert(key, settings.value(key));
        records.append(record);
    }
    settings.endArray();

    int match = -1;
    for (int index = 0; index < records.size(); ++index) {
        if (records.at(index).value(QStringLiteral("owner")).toString() ==
                owner &&
            records.at(index).value(QStringLiteral("name")).toString() ==
                repository) {
            match = index;
            break;
        }
    }
    QVariantMap configured{
        {QStringLiteral("owner"), owner},
        {QStringLiteral("name"), repository},
        {QStringLiteral("cloneUrl"), source},
        {QStringLiteral("localPath"), QString()},
        {QStringLiteral("mirrorPath"), mirror},
        {QStringLiteral("publishToNetwork"), false},
        {QStringLiteral("isPrivate"), false},
        {QStringLiteral("actionsEnabled"), enabled},
        {QStringLiteral("externallyManagedActions"), true},
        {QStringLiteral("externalActionsSource"), source},
        {QStringLiteral("externalActionsRef"),
         QStringLiteral("refs/heads/") + branch},
        {QStringLiteral("secretScanningEnabled"), true},
    };
    if (match >= 0) {
        for (auto it = configured.constBegin(); it != configured.constEnd();
             ++it)
            records[match].insert(it.key(), it.value());
    } else {
        records.append(configured);
    }

    settings.beginWriteArray(QString::fromLatin1(repositoriesKey),
                             records.size());
    for (int index = 0; index < records.size(); ++index) {
        settings.setArrayIndex(index);
        const QVariantMap record = records.at(index);
        for (auto it = record.constBegin(); it != record.constEnd(); ++it)
            settings.setValue(it.key(), it.value());
    }
    settings.endArray();
    return true;
}

bool restrictSettingsFile(QSettings &settings)
{
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;
    const QString fileName = settings.fileName();
    if (fileName.isEmpty())
        return true;
    QFileInfo info(fileName);
    if (!info.exists())
        return false;
    return QFile::setPermissions(
        fileName, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

} // namespace

QJsonObject applyConfiguration(const QJsonObject &request,
                               QSettings &settings)
{
    const QString requestId = safeRequestId(request);
    const QString node = safeNode(request);
    const bool requestedEnabled =
        request.value(QStringLiteral("actionsEnabled")).toBool(false);
    const QSet<QString> required{
        QStringLiteral("type"), QStringLiteral("schemaVersion"),
        QStringLiteral("requestId"), QStringLiteral("node"),
        QStringLiteral("actionsEnabled")};
    const QSet<QString> optional{QStringLiteral("variables")};
    if (!exactFields(request, required, optional) ||
        request.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-actions-configuration") ||
        request.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        node.isEmpty() ||
        !request.value(QStringLiteral("actionsEnabled")).isBool()) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("invalid_request"));
    }

    const QString machine =
        settings.value(QStringLiteral("node/machineName")).toString().trimmed();
    const QString account =
        settings.value(QStringLiteral("account/nodeName")).toString().trimmed();
    const QString localNode = !machine.isEmpty() ? machine : account;
    if (!kNodeName.match(localNode).hasMatch() || localNode != node) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("node_mismatch"));
    }
    // Refuse before changing any executable state unless the existing local
    // settings store can be synchronized and restricted to its owner.
    if (!restrictSettingsFile(settings)) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("settings_write_failed"));
    }

    bool replaceVariables = false;
    QMap<QString, QString> variables;
    if (request.contains(QStringLiteral("variables"))) {
        const QJsonObject envelope =
            request.value(QStringLiteral("variables")).toObject();
        if (!request.value(QStringLiteral("variables")).isObject() ||
            !exactFields(
                envelope,
                {QStringLiteral("mode"), QStringLiteral("values")}) ||
            envelope.value(QStringLiteral("mode")).toString() !=
                QLatin1String("replace") ||
            !envelope.value(QStringLiteral("values")).isObject()) {
            return result(requestId, node, requestedEnabled, false, 0, false,
                          QStringLiteral("invalid_variables"));
        }
        const QJsonObject values =
            envelope.value(QStringLiteral("values")).toObject();
        if (values.size() > kMaximumVariables) {
            return result(requestId, node, requestedEnabled, false, 0, false,
                          QStringLiteral("invalid_variables"));
        }
        qsizetype totalBytes = 0;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!kVariableName.match(it.key()).hasMatch() ||
                !it.value().isString()) {
                return result(requestId, node, requestedEnabled, false, 0,
                              false,
                              QStringLiteral("invalid_variables"));
            }
            const QString value = it.value().toString();
            const qsizetype bytes = value.toUtf8().size();
            totalBytes += bytes;
            if (value.contains(QChar::Null) ||
                bytes > kMaximumVariableValueBytes ||
                totalBytes > kMaximumVariableBytes) {
                return result(requestId, node, requestedEnabled, false, 0,
                              false,
                              QStringLiteral("invalid_variables"));
            }
            variables.insert(it.key(), value);
        }
        replaceVariables = true;
    }

    QString gatewayPath;
    const QJsonObject gateway = readGatewayConfiguration(&gatewayPath);
    const QString source =
        gateway.value(QStringLiteral("sourceRepository")).toString().trimmed();
    const QString repository =
        gateway.value(QStringLiteral("repositoryName")).toString().trimmed();
    const QString owner = repositoryOwner(gateway);
    QString branch = gateway.value(QStringLiteral("catalog"))
                         .toObject()
                         .value(QStringLiteral("branch"))
                         .toString()
                         .trimmed();
    if (branch.isEmpty())
        branch = QStringLiteral("main");
    if (gateway.isEmpty() ||
        gateway.value(QStringLiteral("nodeOwner")).toString().trimmed() !=
            node ||
        owner.isEmpty() ||
        !kRepositoryName.match(repository).hasMatch() ||
        !kBranchName.match(branch).hasMatch() ||
        !safeAbsolutePath(source)) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("mirror_config_unavailable"));
    }
    const QString mirror =
        actionMirrorPath(settings, node, owner, repository);
    if (!prepareActionMirror(source, mirror)) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("actions_mirror_unavailable"));
    }

    if (!upsertRepository(settings, owner, repository, source, mirror, branch,
                          requestedEnabled)) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("settings_write_failed"));
    }
    settings.setValue(QString::fromLatin1(kEnabledSetting), requestedEnabled);
    settings.setValue(QString::fromLatin1(kGenerationSetting), requestId);
    settings.setValue(QString::fromLatin1(kNodeSetting), node);
    settings.setValue(QStringLiteral("actions/mirrorRefreshConfigPath"),
                      gatewayPath);
    settings.setValue(QStringLiteral("actions/mirrorStatePath"),
                      QDir(QFileInfo(gatewayPath).absolutePath())
                          .filePath(QStringLiteral("actions-state.json")));
    settings.setValue(QStringLiteral("actions/mirrorConfiguredAt"),
                      QDateTime::currentMSecsSinceEpoch());
    if (replaceVariables) {
        QJsonObject object;
        for (auto it = variables.constBegin(); it != variables.constEnd();
             ++it)
            object.insert(it.key(), it.value());
        settings.setValue(
            QStringLiteral("actions/variables"),
            QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
    if (!restrictSettingsFile(settings)) {
        return result(requestId, node, requestedEnabled, false, 0, false,
                      QStringLiteral("settings_write_failed"));
    }

    for (QString &value : variables)
        value.fill(QChar::Null);
    variables.clear();
    return result(requestId, node, requestedEnabled, replaceVariables,
                  replaceVariables
                      ? request.value(QStringLiteral("variables"))
                            .toObject()
                            .value(QStringLiteral("values"))
                            .toObject()
                            .size()
                      : 0,
                  true);
}

int runConfigurationStdin(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ForkMesh"));
    app.setOrganizationName(QStringLiteral("ForkMesh"));
    QByteArray input;
    char buffer[4096];
    while (!std::feof(stdin) && input.size() <= kMaximumRequestBytes) {
        const std::size_t count =
            std::fread(buffer, 1, sizeof(buffer), stdin);
        if (count > 0)
            input.append(buffer, int(count));
        if (std::ferror(stdin))
            break;
    }
    QJsonObject request;
    if (!input.isEmpty() && input.size() <= kMaximumRequestBytes) {
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(input, &parseError);
        if (parseError.error == QJsonParseError::NoError &&
            document.isObject())
            request = document.object();
    }
    input.fill('\0');
    input.clear();
    QSettings settings;
    const QJsonObject response = applyConfiguration(request, settings);
    QByteArray encoded =
        QJsonDocument(response)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals);
    std::fputs("FORKMESH_ACTIONS_RESULT=", stdout);
    std::fwrite(encoded.constData(), 1, std::size_t(encoded.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    const bool ok = response.value(QStringLiteral("ok")).toBool();
    encoded.fill('\0');
    return ok ? 0 : 2;
}

} // namespace forkmesh::mirror_actions
