#include "ControlNode.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace forkmesh::control {
namespace {

bool isDnsName(const QString &value)
{
    if (value.isEmpty() || value.size() > 253 ||
        value.startsWith(QLatin1Char('.')) ||
        value.endsWith(QLatin1Char('.')) ||
        value.contains(QStringLiteral(".."))) {
        return false;
    }
    static const QRegularExpression labelPattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    const QStringList labels = value.split(QLatin1Char('.'));
    if (labels.size() < 2)
        return false;
    for (const QString &label : labels) {
        if (!labelPattern.match(label).hasMatch())
            return false;
    }
    return true;
}

QString normalizedDnsName(const QString &value)
{
    return value.trimmed().toLower();
}

bool isBase64UrlPublicKey(const QString &value)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{43}$"))
             .match(value)
             .hasMatch()) {
        return false;
    }
    return QByteArray::fromBase64(
               value.toLatin1(), QByteArray::Base64UrlEncoding)
               .size() == 32;
}

bool isSafeMainRelayUrl(const QString &value)
{
    if (value.trimmed().isEmpty())
        return true;
    const QUrl url(value.trimmed());
    if (!url.isValid() || url.scheme() != QLatin1String("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty()) {
        return false;
    }
    return url.path().isEmpty() || url.path() == QLatin1String("/");
}

QString canonicalCandidate(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable())
        return {};
    return info.canonicalFilePath();
}

QStringList installedToolCandidates(const QString &fileName,
                                    const QString &applicationDir)
{
    QStringList candidates;
    const QString resourceOverride =
        qEnvironmentVariable("FORKMESH_RESOURCE_DIR").trimmed();
    if (!resourceOverride.isEmpty()) {
        candidates.append(
            QDir(resourceOverride)
                .absoluteFilePath(QStringLiteral("tools/") + fileName));
    }
    const QString appDir =
        applicationDir.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : applicationDir.trimmed();
    // Linux CMake installs use <prefix>/bin + <prefix>/share/forkmesh.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../share/forkmesh/tools/") + fileName));
    // A macOS bundle uses ForkMesh.app/Contents/{MacOS,Resources}.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../Resources/forkmesh/tools/") + fileName));
    // The per-user Windows/NSIS layout keeps resources beside the executable.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("resources/forkmesh/tools/") + fileName));
    return candidates;
}

bool hasCompleteWorkerBundle(const QString &scriptPath)
{
    const QDir root(QFileInfo(scriptPath).absoluteDir().absoluteFilePath(
        QStringLiteral("..")));
    const QDir worker(root.absoluteFilePath(QStringLiteral("cloudflare_worker")));
    const QStringList files{
        QStringLiteral("wrangler.toml"),
        QStringLiteral("pywrangler.sh"),
        QStringLiteral("pyproject.toml"),
        QStringLiteral("tools/build_dashboard_assets.py"),
        QStringLiteral("src/entry.py"),
    };
    for (const QString &file : files) {
        const QFileInfo info(worker.absoluteFilePath(file));
        if (!info.isFile() || !info.isReadable())
            return false;
    }
    for (const QString &directory :
         {QStringLiteral("public"), QStringLiteral("migrations")}) {
        const QFileInfo info(worker.absoluteFilePath(directory));
        if (!info.isDir() || !info.isReadable())
            return false;
    }
    return true;
}

bool appendPythonJsonString(const QString &value, QByteArray *output)
{
    if (!output)
        return false;
    output->append('"');
    for (qsizetype i = 0; i < value.size(); ++i) {
        const ushort code = value.at(i).unicode();
        switch (code) {
        case '"':
            output->append("\\\"");
            break;
        case '\\':
            output->append("\\\\");
            break;
        case '\b':
            output->append("\\b");
            break;
        case '\f':
            output->append("\\f");
            break;
        case '\n':
            output->append("\\n");
            break;
        case '\r':
            output->append("\\r");
            break;
        case '\t':
            output->append("\\t");
            break;
        default:
            if (code < 0x20 || code >= 0x7f) {
                output->append("\\u");
                output->append(
                    QByteArray::number(code, 16).rightJustified(4, '0'));
            } else {
                output->append(char(code));
            }
            break;
        }
    }
    output->append('"');
    return true;
}

bool appendPythonCanonicalJson(const QJsonValue &value, QByteArray *output,
                               QString *error)
{
    if (value.isNull() || value.isUndefined()) {
        output->append("null");
        return true;
    }
    if (value.isBool()) {
        output->append(value.toBool() ? "true" : "false");
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const qint64 integer = qint64(number);
        if (!std::isfinite(number) || double(integer) != number) {
            if (error)
                *error = QStringLiteral(
                    "Catalog canonical JSON accepts integer numbers only.");
            return false;
        }
        output->append(QByteArray::number(integer));
        return true;
    }
    if (value.isString())
        return appendPythonJsonString(value.toString(), output);
    if (value.isArray()) {
        output->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            if (i)
                output->append(',');
            if (!appendPythonCanonicalJson(array.at(i), output, error))
                return false;
        }
        output->append(']');
        return true;
    }
    if (value.isObject()) {
        output->append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const QString &key : keys) {
            if (!first)
                output->append(',');
            first = false;
            appendPythonJsonString(key, output);
            output->append(':');
            if (!appendPythonCanonicalJson(object.value(key), output, error))
                return false;
        }
        output->append('}');
        return true;
    }
    if (error)
        *error = QStringLiteral("Catalog canonical JSON contains an unsupported value.");
    return false;
}

QJsonValue optionalBoundedTelemetryInteger(const QJsonValue &value,
                                           qint64 maximum)
{
    if (!value.isDouble())
        return QJsonValue(QJsonValue::Null);
    const double raw = value.toDouble();
    if (!std::isfinite(raw) || raw < 0.0 || std::floor(raw) != raw)
        return QJsonValue(QJsonValue::Null);
    return QJsonValue(qMin(raw, double(maximum)));
}

QPair<QJsonValue, QJsonValue> normalizedTelemetryUsagePair(
    const QJsonValue &usedValue, const QJsonValue &totalValue)
{
    constexpr qint64 kMaximumReportedBytes = qint64(1) << 50;
    QJsonValue used =
        optionalBoundedTelemetryInteger(usedValue, kMaximumReportedBytes);
    const QJsonValue total =
        optionalBoundedTelemetryInteger(totalValue, kMaximumReportedBytes);
    if (used.isNull() || total.isNull() || total.toDouble() <= 0.0) {
        return {QJsonValue(QJsonValue::Null),
                QJsonValue(QJsonValue::Null)};
    }
    if (used.toDouble() > total.toDouble())
        used = total;
    return {used, total};
}

bool isSafeSshEndpoint(const QString &host, const QString &sshUser)
{
    static const QRegularExpression hostPattern(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9.:-]{0,251}[A-Za-z0-9])?$"));
    static const QRegularExpression userPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$"));
    const QString cleanHost = host.trimmed();
    return cleanHost.size() <= 253 &&
           !cleanHost.contains(QStringLiteral("..")) &&
           hostPattern.match(cleanHost).hasMatch() &&
           userPattern.match(sshUser.trimmed()).hasMatch();
}

QJsonArray scrubSavedHostPasswords(
    const QJsonArray &hosts, QHash<QString, QString> *sessionPasswords,
    bool *changed)
{
    static const QStringList secretFields{
        QStringLiteral("pass"), QStringLiteral("password"),
        QStringLiteral("sshPassword"), QStringLiteral("adminPassword")};
    QJsonArray cleanHosts;
    bool removedSecret = false;
    for (const QJsonValue &value : hosts) {
        if (!value.isObject()) {
            cleanHosts.append(value);
            continue;
        }
        QJsonObject host = value.toObject();
        QString migratedPassword;
        for (const QString &field : secretFields) {
            if (!host.contains(field))
                continue;
            if (migratedPassword.isEmpty())
                migratedPassword = host.value(field).toString();
            host.remove(field);
            removedSecret = true;
        }
        if (sessionPasswords && !migratedPassword.isEmpty()) {
            const QString key = savedHostCredentialKey(
                host.value(QStringLiteral("name")).toString(),
                host.value(QStringLiteral("ip")).toString(),
                host.value(QStringLiteral("user")).toString());
            QString replacedPassword = sessionPasswords->take(key);
            replacedPassword.fill(QChar::Null);
            sessionPasswords->insert(key, migratedPassword);
        }
        cleanHosts.append(host);
    }
    if (changed)
        *changed = removedSecret;
    return cleanHosts;
}

} // namespace

HostSshCommand buildHostSshCommand(const QString &host,
                                   const QString &sshUser,
                                   const QString &sshPassword,
                                   const QString &remoteCommand,
                                   QString *error,
                                   const QString &identityFile)
{
    HostSshCommand command;
    if (!isSafeSshEndpoint(host, sshUser)) {
        if (error)
            *error = QStringLiteral("The saved SSH host or username is invalid.");
        return command;
    }
    if (remoteCommand.trimmed().isEmpty() ||
        remoteCommand.contains(QChar::Null)) {
        if (error)
            *error = QStringLiteral("The remote SSH command is invalid.");
        return command;
    }

    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString sshStateDir =
        QDir(appDataDir).filePath(QStringLiteral("ssh"));
    if (appDataDir.isEmpty() || !QDir().mkpath(sshStateDir)) {
        if (error)
            *error = QStringLiteral(
                "ForkMesh could not create its persistent SSH trust store.");
        return command;
    }
    QFile::setPermissions(
        sshStateDir,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner);
    const QString knownHostsPath =
        QDir(sshStateDir).filePath(QStringLiteral("known_hosts"));
    if (!QFileInfo::exists(knownHostsPath)) {
        QFile knownHosts(knownHostsPath);
        if (!knownHosts.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (error) {
                *error = QStringLiteral(
                    "ForkMesh could not initialize its persistent SSH trust "
                    "store.");
            }
            return command;
        }
        knownHosts.close();
    }
    QFile::setPermissions(knownHostsPath,
                          QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner);

    QStringList arguments{
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"),
        QStringLiteral("UserKnownHostsFile=") + knownHostsPath,
        QStringLiteral("-o"), QStringLiteral("HashKnownHosts=yes"),
        QStringLiteral("-o"), QStringLiteral("UpdateHostKeys=yes"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=30"),
    };
    // Pin authentication to a ForkMesh-managed key when the saved host records
    // one (auto-provisioned Vultr mirrors). -i is argv-safe for any path;
    // IdentitiesOnly stops the agent offering unrelated keys first.
    const QString identity = identityFile.trimmed();
    if (!identity.isEmpty()) {
        if (identity.contains(QChar::Null) ||
            !QFileInfo(identity).isFile()) {
            if (error)
                *error = QStringLiteral(
                    "The managed SSH key for this host is missing.");
            return command;
        }
        arguments << QStringLiteral("-i") << identity
                  << QStringLiteral("-o")
                  << QStringLiteral("IdentitiesOnly=yes");
    }
    command.environment = QProcessEnvironment::systemEnvironment();
    command.environment.remove(QStringLiteral("SSHPASS"));
    if (sshPassword.isEmpty()) {
        command.program = QStringLiteral("ssh");
        arguments << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
                  << QStringLiteral("-o")
                  << QStringLiteral("PreferredAuthentications=publickey");
    } else {
        command.program = QStringLiteral("sshpass");
        command.environment.insert(QStringLiteral("SSHPASS"), sshPassword);
        arguments.prepend(QStringLiteral("ssh"));
        arguments.prepend(QStringLiteral("-e"));
        // Let an agent/default key win when available, then use the
        // session-only password. Never disable public-key authentication.
        arguments << QStringLiteral("-o")
                  << QStringLiteral(
                         "PreferredAuthentications=publickey,password");
    }
    arguments << sshUser.trimmed() + QLatin1Char('@') + host.trimmed()
              << remoteCommand;
    command.arguments = arguments;
    if (error)
        error->clear();
    return command;
}

QString savedHostCredentialKey(const QString &nodeName, const QString &host,
                               const QString &sshUser)
{
    const QJsonArray identity{
        nodeName.trimmed(), host.trimmed().toLower(), sshUser.trimmed()};
    return QString::fromUtf8(
        QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

QString sshConnectionFailureHint(int exitCode, const QString &outputTail)
{
    if (exitCode != 255)
        return {};
    const QString tail = outputTail.toLower();
    if (tail.contains(QStringLiteral("connection timed out")) ||
        tail.contains(QStringLiteral("operation timed out"))) {
        return QStringLiteral(
            "The connection to port 22 timed out \xE2\x80\x94 packets are "
            "being dropped, not rejected. This almost always means a firewall "
            "or cloud security group between here and the host is blocking "
            "SSH: check the provider's firewall/security-group rules for this "
            "instance (and any local network firewall) allow inbound TCP 22 "
            "from your IP, then confirm the address is correct and the host "
            "has finished booting.");
    }
    if (tail.contains(QStringLiteral("connection refused"))) {
        return QStringLiteral(
            "The host actively refused the connection on port 22 \xE2\x80\x94 "
            "SSH is not listening yet (a freshly booted instance can take a "
            "minute or two) or a firewall rule is rejecting the port outright. "
            "Wait a moment and retry; if it persists, check the provider's "
            "firewall/security-group settings for this instance.");
    }
    if (tail.contains(QStringLiteral("no route to host"))) {
        return QStringLiteral(
            "There is no network route to this host \xE2\x80\x94 check that "
            "the address is correct and that a firewall or security group "
            "is not dropping the traffic.");
    }
    if (tail.contains(QStringLiteral("host key verification failed"))) {
        return QStringLiteral(
            "The host's SSH key does not match the one ForkMesh already "
            "trusts for it \xE2\x80\x94 this usually means the instance was "
            "rebuilt/reinstalled at the same address. Remove the stale entry "
            "from this app's managed known_hosts file if you intended that, "
            "then retry.");
    }
    if (tail.contains(QStringLiteral("permission denied"))) {
        return QStringLiteral(
            "The host rejected the credentials \xE2\x80\x94 double-check the "
            "SSH user, password, and key for this saved host.");
    }
    return {};
}

QJsonArray loadSavedHosts(QSettings &settings, const QString &settingsKey,
                          QHash<QString, QString> *sessionPasswords)
{
    const QJsonArray stored =
        QJsonDocument::fromJson(
            settings.value(settingsKey).toString().toUtf8())
            .array();
    bool changed = false;
    const QJsonArray clean =
        scrubSavedHostPasswords(stored, sessionPasswords, &changed);
    if (changed) {
        settings.setValue(
            settingsKey,
            QString::fromUtf8(
                QJsonDocument(clean).toJson(QJsonDocument::Compact)));
        settings.sync();
    }
    return clean;
}

void saveSavedHosts(QSettings &settings, const QString &settingsKey,
                    const QJsonArray &hosts)
{
    const QJsonArray clean =
        scrubSavedHostPasswords(hosts, nullptr, nullptr);
    settings.setValue(
        settingsKey,
        QString::fromUtf8(
            QJsonDocument(clean).toJson(QJsonDocument::Compact)));
    settings.sync();
}

QString validateMirrorActionsConfigurationRequest(
    const MirrorActionsConfigurationRequest &request)
{
    static const QRegularExpression requestIdPattern(
        QStringLiteral("^[a-f0-9]{32}$"));
    static const QRegularExpression hostPattern(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9.:-]{0,251}[A-Za-z0-9])?$"));
    static const QRegularExpression userPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$"));
    static const QRegularExpression nodePattern(
        QStringLiteral("^[a-z][a-z0-9-]{0,62}$"));
    static const QRegularExpression variablePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,63}$"));

    if (!requestIdPattern.match(request.requestId).hasMatch())
        return QStringLiteral("The Actions request ID is invalid.");
    const QString host = request.host.trimmed();
    if (host.size() > 253 || host.contains(QStringLiteral("..")) ||
        !hostPattern.match(host).hasMatch()) {
        return QStringLiteral("The saved host address is invalid.");
    }
    if (!userPattern.match(request.sshUser.trimmed()).hasMatch())
        return QStringLiteral("The saved SSH username is invalid.");
    if (!nodePattern.match(request.nodeName.trimmed()).hasMatch())
        return QStringLiteral("The mirror node name is invalid.");
    if (!request.replaceVariables && !request.variables.isEmpty()) {
        return QStringLiteral(
            "Variable values require explicit replace-variables approval.");
    }
    if (request.variables.size() > 64)
        return QStringLiteral("At most 64 Actions variables can be sent at once.");

    qsizetype totalValueBytes = 0;
    for (auto it = request.variables.constBegin();
         it != request.variables.constEnd(); ++it) {
        if (!variablePattern.match(it.key()).hasMatch()) {
            return QStringLiteral(
                "Actions variable names must use letters, digits, and "
                "underscores, and cannot start with a digit.");
        }
        if (it.value().contains(QChar::Null)) {
            return QStringLiteral(
                "Actions variable values cannot contain NUL characters.");
        }
        const qsizetype valueBytes = it.value().toUtf8().size();
        if (valueBytes > 16 * 1024) {
            return QStringLiteral(
                "Each Actions variable must be 16 KiB or smaller.");
        }
        totalValueBytes += valueBytes;
        if (totalValueBytes > 48 * 1024) {
            return QStringLiteral(
                "The combined Actions variable values must be 48 KiB or smaller.");
        }
    }
    return {};
}

QByteArray buildMirrorActionsConfigurationPayload(
    const MirrorActionsConfigurationRequest &request, QString *error)
{
    const QString validation =
        validateMirrorActionsConfigurationRequest(request);
    if (!validation.isEmpty()) {
        if (error)
            *error = validation;
        return {};
    }

    QJsonObject root{
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.mirror-actions-configuration")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("requestId"), request.requestId},
        {QStringLiteral("node"), request.nodeName.trimmed()},
        {QStringLiteral("actionsEnabled"), request.actionsEnabled},
    };
    if (request.replaceVariables) {
        QJsonObject values;
        for (auto it = request.variables.constBegin();
             it != request.variables.constEnd(); ++it) {
            values.insert(it.key(), it.value());
        }
        root.insert(
            QStringLiteral("variables"),
            QJsonObject{
                {QStringLiteral("mode"), QStringLiteral("replace")},
                {QStringLiteral("values"), values},
            });
    }
    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (payload.size() > 64 * 1024) {
        payload.fill('\0');
        payload.clear();
        if (error)
            *error = QStringLiteral(
                "The Actions configuration payload exceeds 64 KiB.");
        return {};
    }
    if (error)
        error->clear();
    return payload;
}

MirrorActionsSshCommand buildMirrorActionsSshCommand(
    const MirrorActionsConfigurationRequest &request,
    const QString &sshPassword, QString *error,
    const QString &identityFile)
{
    MirrorActionsSshCommand command;
    command.standardInput =
        buildMirrorActionsConfigurationPayload(request, error);
    if (command.standardInput.isEmpty())
        return command;

    const QString remoteCommand = QStringLiteral(
        "if command -v forkmesh >/dev/null 2>&1; then "
        "exec forkmesh --configure-mirror-actions-stdin; "
        "elif [ -x \"$HOME/.local/bin/forkmesh\" ]; then "
        "exec \"$HOME/.local/bin/forkmesh\" "
        "--configure-mirror-actions-stdin; "
        "else printf 'ForkMesh Actions helper is not installed.\\n' >&2; "
        "exit 127; fi");
    const HostSshCommand ssh = buildHostSshCommand(
        request.host, request.sshUser, sshPassword, remoteCommand, error,
        identityFile);
    if (ssh.program.isEmpty()) {
        command.standardInput.fill('\0');
        command.standardInput.clear();
        return command;
    }
    command.program = ssh.program;
    command.arguments = ssh.arguments;
    command.environment = ssh.environment;
    if (error)
        error->clear();
    return command;
}

QJsonObject parseMirrorActionsConfigurationResult(
    const QByteArray &output, const QString &expectedRequestId,
    const QString &expectedNodeName, QString *error)
{
    constexpr qsizetype kMaximumOutputBytes = 64 * 1024;
    constexpr auto kPrefix = "FORKMESH_ACTIONS_RESULT=";
    if (output.size() > kMaximumOutputBytes) {
        if (error)
            *error = QStringLiteral(
                "The mirror Actions helper returned too much output.");
        return {};
    }
    QByteArray encoded;
    int sentinels = 0;
    const QList<QByteArray> lines = output.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.startsWith(kPrefix))
            continue;
        ++sentinels;
        encoded = line.mid(qstrlen(kPrefix));
    }
    if (sentinels != 1 || encoded.isEmpty() || encoded.size() > 4096) {
        if (error)
            *error = QStringLiteral(
                "The mirror Actions helper did not return one valid result.");
        return {};
    }
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$"))
             .match(QString::fromLatin1(encoded))
             .hasMatch()) {
        if (error)
            *error = QStringLiteral(
                "The mirror Actions helper result encoding is invalid.");
        return {};
    }
    const QByteArray decoded = QByteArray::fromBase64(
        encoded, QByteArray::Base64UrlEncoding |
                     QByteArray::AbortOnBase64DecodingErrors);
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(decoded, &parseError);
    const QJsonObject result = document.object();
    static const QSet<QString> allowedKeys{
        QStringLiteral("type"),
        QStringLiteral("schemaVersion"),
        QStringLiteral("ok"),
        QStringLiteral("requestId"),
        QStringLiteral("node"),
        QStringLiteral("actionsEnabled"),
        QStringLiteral("variablesReplaced"),
        QStringLiteral("variableCount"),
        QStringLiteral("errorCode"),
    };
    bool hasUnexpectedKey = false;
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            hasUnexpectedKey = true;
            break;
        }
    }
    const QJsonValue variableCount =
        result.value(QStringLiteral("variableCount"));
    const bool validCount =
        variableCount.isDouble() &&
        variableCount.toDouble() >= 0 &&
        variableCount.toDouble() <= 64 &&
        std::floor(variableCount.toDouble()) == variableCount.toDouble();
    const QString errorCode =
        result.value(QStringLiteral("errorCode")).toString();
    static const QRegularExpression errorCodePattern(
        QStringLiteral("^[a-z][a-z0-9_]{0,63}$"));
    const bool validErrorCode =
        errorCode.isEmpty() ||
        errorCodePattern.match(errorCode).hasMatch();
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject() ||
        hasUnexpectedKey ||
        result.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-actions-configuration-result") ||
        result.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        result.value(QStringLiteral("ok")).isBool() == false ||
        result.value(QStringLiteral("requestId")).toString() !=
            expectedRequestId ||
        result.value(QStringLiteral("node")).toString() !=
            expectedNodeName ||
        !result.value(QStringLiteral("actionsEnabled")).isBool() ||
        !result.value(QStringLiteral("variablesReplaced")).isBool() ||
        !validCount || !validErrorCode) {
        if (error)
            *error = QStringLiteral(
                "The mirror Actions helper result failed validation.");
        return {};
    }
    if (error)
        error->clear();
    return result;
}

QString validateCloudflareBootstrapRequest(
    const CloudflareBootstrapRequest &request,
    bool allowAutomaticTopology)
{
    const QString hostname = normalizedDnsName(request.hostname);
    const QString zone = normalizedDnsName(request.zoneName);
    const bool automaticTopology =
        allowAutomaticTopology && hostname.isEmpty() && zone.isEmpty();
    if (!automaticTopology) {
        if (!isDnsName(hostname))
            return QStringLiteral(
                "Enter a DNS hostname such as mirror.example.com (without a URL or path).");
        if (!isDnsName(zone))
            return QStringLiteral(
                "Enter the Cloudflare zone name such as example.com.");
        if (hostname != zone &&
            !hostname.endsWith(QLatin1Char('.') + zone)) {
            return QStringLiteral(
                "The deployment hostname must be inside the selected zone.");
        }
    }

    static const QRegularExpression resourceName(
        QStringLiteral("^[a-z][a-z0-9-]{0,62}$"));
    const QString node = request.nodeName.trimmed();
    if (!node.isEmpty() && !resourceName.match(node).hasMatch()) {
        return QStringLiteral(
            "The node name must start with a letter and contain only lowercase "
            "letters, digits, or hyphens (63 characters maximum).");
    }
    const QString account = request.accountId.trimmed();
    static const QRegularExpression accountId(
        QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    if (!account.isEmpty() && !accountId.match(account).hasMatch())
        return QStringLiteral("The Cloudflare account ID contains invalid characters.");
    const QString label = request.relayLabel.trimmed();
    if (label.size() > 80) {
        return QStringLiteral("The relay label must be 80 characters or fewer.");
    }
    for (const QChar ch : label) {
        if (ch.unicode() < 32 || ch.unicode() == 127)
            return QStringLiteral("The relay label contains a control character.");
    }
    if (!isSafeMainRelayUrl(request.mainRelayUrl)) {
        return QStringLiteral(
            "The upstream relay must be an HTTPS origin with no credentials, "
            "query, fragment, or private path.");
    }
    return {};
}

QString findCloudflareBootstrapScript(const QString &sourceDir,
                                      const QString &applicationDir)
{
    QStringList candidates;
    const QString overridePath =
        qEnvironmentVariable("FORKMESH_CLOUDFLARE_BOOTSTRAP").trimmed();
    if (!overridePath.isEmpty())
        candidates.append(overridePath);

    const QString source = sourceDir.trimmed();
    if (!source.isEmpty()) {
        candidates.append(
            QDir(source).absoluteFilePath(QStringLiteral("../tools/cloudflare_bootstrap.py")));
        candidates.append(
            QDir(source).absoluteFilePath(QStringLiteral("tools/cloudflare_bootstrap.py")));
    }

    const QString appDir = applicationDir.trimmed().isEmpty()
                               ? QCoreApplication::applicationDirPath()
                               : applicationDir.trimmed();
    candidates.append(
        QDir(appDir).absoluteFilePath(QStringLiteral("../tools/cloudflare_bootstrap.py")));
    candidates.append(
        QDir(appDir).absoluteFilePath(QStringLiteral("tools/cloudflare_bootstrap.py")));
    candidates.append(
        installedToolCandidates(
            QStringLiteral("cloudflare_bootstrap.py"), appDir));

    for (const QString &candidate : candidates) {
        const QString resolved = canonicalCandidate(candidate);
        if (!resolved.isEmpty() && hasCompleteWorkerBundle(resolved))
            return resolved;
    }
    return {};
}

QString findCloudflareWorkerDirectory(const QString &sourceDir,
                                      const QString &applicationDir)
{
    const QString bootstrap =
        findCloudflareBootstrapScript(sourceDir, applicationDir);
    if (bootstrap.isEmpty())
        return {};
    const QString workerPath =
        QDir(QFileInfo(bootstrap).absoluteDir().absoluteFilePath(
                 QStringLiteral("..")))
            .absoluteFilePath(QStringLiteral("cloudflare_worker"));
    const QFileInfo worker(workerPath);
    if (!worker.isDir() || !worker.isReadable())
        return {};
    return worker.canonicalFilePath();
}

namespace {

QString findPinnedTool(const QString &fileName, const QString &overrideName,
                       const QString &sourceDir,
                       const QString &applicationDir)
{
    QStringList candidates;
    const QString overridePath =
        qEnvironmentVariable(overrideName.toLatin1().constData()).trimmed();
    if (!overridePath.isEmpty())
        candidates.append(overridePath);
    const QString source = sourceDir.trimmed();
    if (!source.isEmpty()) {
        candidates.append(
            QDir(source).absoluteFilePath(
                QStringLiteral("../tools/") + fileName));
        candidates.append(
            QDir(source).absoluteFilePath(
                QStringLiteral("tools/") + fileName));
    }
    const QString appDir =
        applicationDir.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : applicationDir.trimmed();
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../tools/") + fileName));
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("tools/") + fileName));
    candidates.append(installedToolCandidates(fileName, appDir));
    for (const QString &candidate : candidates) {
        const QString resolved = canonicalCandidate(candidate);
        if (!resolved.isEmpty())
            return resolved;
    }
    return {};
}

} // namespace

QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("cloudflare_tunnel_bootstrap.py"),
        QStringLiteral("FORKMESH_CLOUDFLARE_TUNNEL_BOOTSTRAP"),
        sourceDir, applicationDir);
}

QString findMirrorGatewayScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("mirror_gateway.py"),
        QStringLiteral("FORKMESH_MIRROR_GATEWAY"),
        sourceDir, applicationDir);
}

QString findCloudflaredInstallerScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("cloudflared_install.py"),
        QStringLiteral("FORKMESH_CLOUDFLARED_INSTALLER"),
        sourceDir, applicationDir);
}

QString shlexQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QByteArray pythonCanonicalJson(const QJsonValue &value, QString *error)
{
    QByteArray output;
    if (!appendPythonCanonicalJson(value, &output, error))
        return {};
    if (error)
        error->clear();
    return output;
}

QByteArray catalogV2SigningPayload(QJsonObject normalizedRecord,
                                   QString *error)
{
    normalizedRecord.remove(QStringLiteral("signature"));
    const QByteArray canonical =
        pythonCanonicalJson(normalizedRecord, error);
    if (canonical.isEmpty())
        return {};
    const QByteArray digest =
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-catalog-v2\n") + digest;
}

QJsonObject normalizedCatalogHostTelemetry(const QJsonObject &data)
{
    const auto memoryUsage = normalizedTelemetryUsagePair(
        data.value(QStringLiteral("memUsedBytes")),
        data.value(QStringLiteral("memTotalBytes")));
    const auto diskUsage = normalizedTelemetryUsagePair(
        data.value(QStringLiteral("diskUsedBytes")),
        data.value(QStringLiteral("diskTotalBytes")));
    return {
        {QStringLiteral("cpuPercent"),
         optionalBoundedTelemetryInteger(
             data.value(QStringLiteral("cpuPercent")), 100)},
        {QStringLiteral("memUsedBytes"), memoryUsage.first},
        {QStringLiteral("memTotalBytes"), memoryUsage.second},
        {QStringLiteral("diskUsedBytes"), diskUsage.first},
        {QStringLiteral("diskTotalBytes"), diskUsage.second},
    };
}

QByteArray privateReplicaRouteSigningPayload(
    const QString &owner, const QString &repository, const QString &node,
    const QString &opaqueId, const QString &replicaSha256, quint64 keyEpoch,
    bool active, qint64 issuedAtMs, QString *error)
{
    const auto cleanSegment = [](const QString &value) {
        return !value.trimmed().isEmpty() &&
               !value.contains(QLatin1Char('\n')) &&
               !value.contains(QLatin1Char('\r')) &&
               value == value.trimmed();
    };
    static const QRegularExpression digestPattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    if (!cleanSegment(owner) || !cleanSegment(repository) ||
        !cleanSegment(node) || !digestPattern.match(opaqueId).hasMatch() ||
        !digestPattern.match(replicaSha256).hasMatch() || keyEpoch == 0 ||
        issuedAtMs <= 0) {
        if (error)
            *error = QStringLiteral("The private-replica route fields are invalid.");
        return {};
    }
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-private-route-v1\n") +
           owner.toUtf8() + '\n' + repository.toUtf8() + '\n' +
           node.toUtf8() + '\n' + opaqueId.toUtf8() + '\n' +
           replicaSha256.toUtf8() + '\n' +
           QByteArray::number(keyEpoch) + '\n' +
           (active ? QByteArrayLiteral("1") : QByteArrayLiteral("0")) +
           '\n' + QByteArray::number(issuedAtMs);
}

CloudflareBootstrapCommand buildCloudflareBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey)
{
    CloudflareBootstrapCommand command;
    command.program = pythonProgram.trimmed();
    command.arguments = {scriptPath};
    if (request.hostname.trimmed().isEmpty() &&
        request.zoneName.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--auto-configure");
    } else {
        command.arguments
            << QStringLiteral("--hostname")
            << normalizedDnsName(request.hostname)
            << QStringLiteral("--zone")
            << normalizedDnsName(request.zoneName);
    }
    if (!request.accountId.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--account-id")
                          << request.accountId.trimmed();
    }
    if (!request.nodeName.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--node-name")
                          << request.nodeName.trimmed();
    }
    if (!request.relayLabel.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--relay-label")
                          << request.relayLabel.trimmed();
    }
    // Passing an explicit empty value creates a standalone/main relay; otherwise
    // the bootstrapper links the new relay into the selected upstream mesh.
    command.arguments << QStringLiteral("--main-relay-url")
                      << request.mainRelayUrl.trimmed();
    if (request.dryRun)
        command.arguments << QStringLiteral("--dry-run");
    command.arguments << QStringLiteral("--json-stdout");

    if (!nodePublicKey.trimmed().isEmpty() &&
        !signerProgram.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--mirror-public-key")
                          << nodePublicKey.trimmed()
                          << QStringLiteral("--manifest-signer-command")
                          << (shlexQuote(signerProgram.trimmed()) +
                              QStringLiteral(" --sign-mirror-manifest"));
    } else {
        // A deployment can still provide routing without publishing a trust
        // manifest. The UI normally supplies the local identity signer; this is
        // only the explicit safe fallback when no identity exists.
        command.arguments << QStringLiteral("--skip-mirror-manifest");
    }

    command.environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {
             QStringLiteral("CLOUDFLARE_API_TOKEN"),
             QStringLiteral("CF_API_TOKEN"),
             QStringLiteral("CLOUDFLARE_TOKEN"),
             QStringLiteral("CF_TOKEN"),
         }) {
        command.environment.remove(name);
    }
    command.environment.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"), apiToken);
    return command;
}

namespace {

// First stored value whose name matches one of `names`, case-insensitively and
// in the caller's preference order. Values that span lines (or embed NULs) are
// rejected: they are never a real credential and would break the child
// environment they are destined for.
QString storedCredential(const QMap<QString, QString> &variables,
                         const QStringList &names)
{
    for (const QString &name : names) {
        for (auto it = variables.constBegin(); it != variables.constEnd();
             ++it) {
            if (it.key().trimmed().compare(name, Qt::CaseInsensitive) != 0)
                continue;
            const QString value = it.value().trimmed();
            if (value.isEmpty() || value.contains(QChar(u'\0')) ||
                value.contains(QLatin1Char('\n')) ||
                value.contains(QLatin1Char('\r'))) {
                continue;
            }
            return value;
        }
    }
    return {};
}

}  // namespace

QString cloudflareApiTokenFromVariables(
    const QMap<QString, QString> &variables)
{
    return storedCredential(variables, {
                                           QStringLiteral("CLOUDFLARE_API_TOKEN"),
                                           QStringLiteral("CF_API_TOKEN"),
                                           QStringLiteral("CLOUDFLARE_TOKEN"),
                                           QStringLiteral("CF_TOKEN"),
                                       });
}

QString cloudflareAccountIdFromVariables(
    const QMap<QString, QString> &variables)
{
    return storedCredential(variables,
                            {
                                QStringLiteral("CLOUDFLARE_ACCOUNT_ID"),
                                QStringLiteral("CF_ACCOUNT_ID"),
                                QStringLiteral("CLOUDFLARE_ACCOUNT"),
                            });
}

CloudflareBootstrapCommand buildCloudflareTailCommand(
    const QString &apiToken,
    const QString &accountId,
    const QString &npxProgram)
{
    CloudflareBootstrapCommand command;
    const QString token = apiToken.trimmed();
    const QString account = accountId.trimmed();
    command.program = npxProgram.trimmed();
    if (token.isEmpty() || command.program.isEmpty())
        return command;

    static const QRegularExpression accountPattern(
        QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    if (!account.isEmpty() &&
        !accountPattern.match(account).hasMatch()) {
        command.program.clear();
        return command;
    }

    command.arguments = {
        QStringLiteral("--yes"),
        QStringLiteral("wrangler@4.42.1"),
        QStringLiteral("tail"),
        QStringLiteral("--format"),
        QStringLiteral("pretty"),
    };
    command.environment = QProcessEnvironment::systemEnvironment();
    command.environment.remove(QStringLiteral("CLOUDFLARE_API_KEY"));
    command.environment.remove(QStringLiteral("CLOUDFLARE_EMAIL"));
    command.environment.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"),
                               token);
    if (account.isEmpty()) {
        command.environment.remove(
            QStringLiteral("CLOUDFLARE_ACCOUNT_ID"));
    } else {
        command.environment.insert(
            QStringLiteral("CLOUDFLARE_ACCOUNT_ID"), account);
    }
    return command;
}

QJsonObject parseCloudflareBootstrapResult(const QByteArray &output,
                                           QString *error)
{
    constexpr qsizetype kMaximumOutputBytes = 1024 * 1024;
    const QByteArray prefix("FORKMESH_BOOTSTRAP_RESULT=");
    if (output.size() > kMaximumOutputBytes) {
        if (error)
            *error = QStringLiteral("Cloudflare bootstrap output was too large.");
        return {};
    }
    QByteArray encoded;
    int matches = 0;
    for (const QByteArray &rawLine : output.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (!line.startsWith(prefix))
            continue;
        ++matches;
        encoded = line.mid(prefix.size());
    }
    if (matches != 1 || encoded.isEmpty() || encoded.size() > 256 * 1024) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap returned no unique machine result.");
        return {};
    }
    const QByteArray json = QByteArray::fromBase64(
        encoded, QByteArray::Base64UrlEncoding |
                     QByteArray::AbortOnBase64DecodingErrors);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (json.isEmpty() || parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap returned an invalid machine result.");
        return {};
    }
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap did not confirm a successful result.");
        return {};
    }
    if (error)
        error->clear();
    return object;
}

CloudflareBootstrapCommand buildCloudflareTunnelBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &mirrorHostname,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey,
    const QString &gatewayConfigPath,
    const QString &manifestOutputPath,
    const QString &connectorTokenPath)
{
    CloudflareBootstrapCommand command;
    command.program = pythonProgram.trimmed();
    command.arguments = {
        scriptPath,
        QStringLiteral("--hostname"),
        normalizedDnsName(mirrorHostname),
        QStringLiteral("--zone"),
        normalizedDnsName(request.zoneName),
        QStringLiteral("--node-name"),
        request.nodeName.trimmed().toLower(),
        QStringLiteral("--origin-host"),
        QStringLiteral("127.0.0.1"),
        QStringLiteral("--origin-port"),
        QStringLiteral("8790"),
        QStringLiteral("--gateway-config"),
        gatewayConfigPath,
        QStringLiteral("--mirror-public-key"),
        nodePublicKey.trimmed(),
        QStringLiteral("--manifest-signer-command"),
        shlexQuote(signerProgram.trimmed()) +
            QStringLiteral(" --sign-mirror-manifest"),
        QStringLiteral("--manifest-output"),
        manifestOutputPath,
        QStringLiteral("--tunnel-token-file"),
        connectorTokenPath,
    };
    if (!request.accountId.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--account-id")
                          << request.accountId.trimmed();
    }
    if (request.dryRun)
        command.arguments << QStringLiteral("--dry-run");
    command.environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {
             QStringLiteral("CLOUDFLARE_API_TOKEN"),
             QStringLiteral("CF_API_TOKEN"),
             QStringLiteral("CLOUDFLARE_TOKEN"),
             QStringLiteral("CF_TOKEN"),
         }) {
        command.environment.remove(name);
    }
    command.environment.insert(
        QStringLiteral("CLOUDFLARE_API_TOKEN"), apiToken);
    command.environment.insert(
        QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    return command;
}

QByteArray httpsMirrorRegistrationSigningPayload(
    const QString &node, const QString &baseUrl,
    const QString &publicKey, qint64 issuedAtMs, QString *error)
{
    static const QRegularExpression nodePattern(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9_-]{43}$"));
    QUrl url(baseUrl.trimmed(), QUrl::StrictMode);
    if (!nodePattern.match(node).hasMatch() ||
        !keyPattern.match(publicKey).hasMatch() ||
        !url.isValid() ||
        url.scheme() != QLatin1String("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty() ||
        (url.path() != QLatin1String("") &&
         url.path() != QLatin1String("/")) ||
        (url.port(-1) != -1 && url.port(-1) != 443) ||
        issuedAtMs <= 0) {
        if (error)
            *error = QStringLiteral(
                "The direct-HTTPS endpoint registration fields are invalid.");
        return {};
    }
    url.setScheme(QStringLiteral("https"));
    url.setPath(QString());
    url.setPort(-1);
    const QString normalized =
        url.toString(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash)
            .toLower();
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-https-endpoint-v1\n") +
           node.toUtf8() + '\n' + normalized.toUtf8() + '\n' +
           publicKey.toUtf8() + '\n' +
           QByteArray::number(issuedAtMs);
}

QString redactProcessOutput(const QString &text,
                            const QStringList &exactSecrets)
{
    QString safe = text;
    safe.remove(QChar(u'\0'));
    // Strip terminal control sequences before inserting output into a rich UI.
    static const QRegularExpression ansi(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\))"));
    safe.remove(ansi);

    for (const QString &secret : exactSecrets) {
        if (!secret.isEmpty())
            safe.replace(secret, QStringLiteral("<redacted>"));
    }

    static const QRegularExpression bearer(
        QStringLiteral("(?i)\\bBearer\\s+[A-Za-z0-9._~+/-]{8,}"));
    safe.replace(bearer, QStringLiteral("Bearer <redacted>"));
    static const QRegularExpression namedCredential(
        QStringLiteral(
            "(?i)\\b(CLOUDFLARE_API_TOKEN|CF_API_TOKEN|CLOUDFLARE_TOKEN|"
            "CF_TOKEN|authorization|api[_ -]?token|password|secret)"
            "(\\s*(?::|=)\\s*|\\s+Bearer\\s+)[^\\s,;]+"));
    safe.replace(namedCredential, QStringLiteral("\\1=<redacted>"));
    return safe;
}

bool isValidSolanaPublicAddress(const QString &address)
{
    const QByteArray input = address.trimmed().toLatin1();
    if (input.size() < 32 || input.size() > 44)
        return false;
    static const QByteArray alphabet(
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    QByteArray decoded(1, '\0');
    for (const char ch : input) {
        const int value = alphabet.indexOf(ch);
        if (value < 0)
            return false;
        int carry = value;
        for (int i = decoded.size() - 1; i >= 0; --i) {
            carry += static_cast<unsigned char>(decoded.at(i)) * 58;
            decoded[i] = char(carry & 0xff);
            carry >>= 8;
        }
        while (carry > 0) {
            decoded.prepend(char(carry & 0xff));
            carry >>= 8;
        }
    }
    int leadingZeroes = 0;
    while (leadingZeroes < input.size() && input.at(leadingZeroes) == '1')
        ++leadingZeroes;
    while (!decoded.isEmpty() && decoded.front() == '\0')
        decoded.remove(0, 1);
    decoded.prepend(QByteArray(leadingZeroes, '\0'));
    return decoded.size() == 32;
}

QUrl worldUrlForRelay(const QString &relayUrl)
{
    QString value = relayUrl.trimmed();
    if (value.isEmpty())
        return {};
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("https://"));
    QUrl url(value);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() == QLatin1String("wss"))
        url.setScheme(QStringLiteral("https"));
    else if (url.scheme() == QLatin1String("ws"))
        url.setScheme(QStringLiteral("http"));
    if (url.scheme() != QLatin1String("https") &&
        url.scheme() != QLatin1String("http")) {
        return {};
    }
    url.setUserInfo(QString());
    url.setPath(QStringLiteral("/world/"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QUrl worldDevServerUrl(const QString &configured)
{
    QString value = configured.trimmed();
    if (value.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0)
        return {};
    if (value.isEmpty())
        value = QStringLiteral("http://127.0.0.1:8788/world/");
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("http://"));
    QUrl url(value);
    if (!url.isValid() || url.scheme() != QLatin1String("http"))
        return {};
    const QString host = url.host().toLower();
    if (host != QLatin1String("localhost") &&
        host != QLatin1String("127.0.0.1") && host != QLatin1String("::1")) {
        return {};
    }
    url.setUserInfo(QString());
    if (url.path().isEmpty() || url.path() == QLatin1String("/"))
        url.setPath(QStringLiteral("/world/"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QStringList directMirrorRepositoryOwners(const QString &canonicalOwner,
                                         const QString &catalogOwner)
{
    static const QRegularExpression ownerPattern(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    QStringList owners;
    for (const QString &raw : {canonicalOwner, catalogOwner}) {
        const QString owner = raw.trimmed().toLower();
        if (ownerPattern.match(owner).hasMatch() &&
            !owners.contains(owner)) {
            owners.append(owner);
        }
    }
    return owners;
}

QByteArray mirrorManifestSigningPayload(const QJsonObject &request,
                                        const QString &expectedPublicKey,
                                        QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    if (!isBase64UrlPublicKey(expectedPublicKey))
        return fail(QStringLiteral("The local identity public key is invalid."));
    if (request.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        request.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-endpoint-signing-request") ||
        request.value(QStringLiteral("algorithm")).toString() !=
            QLatin1String("Ed25519") ||
        request.value(QStringLiteral("encoding")).toString() !=
            QLatin1String("base64url-no-padding") ||
        request.value(QStringLiteral("canonicalization")).toString() !=
            QLatin1String("forkmesh-json-sort-v1")) {
        return fail(QStringLiteral("The manifest signing request has an unsupported format."));
    }
    if (request.value(QStringLiteral("publicKey")).toString() !=
        expectedPublicKey) {
        return fail(QStringLiteral("The manifest signing request targets another identity."));
    }
    const QString encoded =
        request.value(QStringLiteral("payloadBase64")).toString();
    if (encoded.isEmpty() || encoded.contains(QLatin1Char('=')) ||
        !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$"))
             .match(encoded)
             .hasMatch()) {
        return fail(QStringLiteral("The manifest signing payload is not valid base64url."));
    }
    const QByteArray payload = QByteArray::fromBase64(
        encoded.toLatin1(), QByteArray::Base64UrlEncoding);
    if (payload.isEmpty() || payload.size() > 64 * 1024)
        return fail(QStringLiteral("The manifest signing payload size is invalid."));
    const QString expectedDigest =
        QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    const QString suppliedDigest =
        request.value(QStringLiteral("payloadSha256")).toString().toLower();
    if (suppliedDigest.size() != 64 || suppliedDigest != expectedDigest) {
        return fail(QStringLiteral("The manifest signing payload checksum does not match."));
    }
    const QJsonDocument manifest = QJsonDocument::fromJson(payload);
    if (!manifest.isObject())
        return fail(QStringLiteral("The manifest signing payload is not a JSON object."));
    const QJsonObject root = manifest.object();
    if (root.contains(QStringLiteral("signature")) ||
        root.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-endpoint")) {
        return fail(QStringLiteral("The manifest signing payload has an invalid type."));
    }
    if (error)
        error->clear();
    return payload;
}

QString validateVultrMirrorRequest(const QString &apiKey,
                                   const QString &nodeName)
{
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9]{20,128}$"));
    if (!keyPattern.match(apiKey.trimmed()).hasMatch()) {
        return QStringLiteral(
            "Enter your Vultr API key (Account \xE2\x86\x92 API in the Vultr "
            "panel). It is used from memory only and never saved to disk.");
    }
    static const QRegularExpression nodePattern(
        QStringLiteral("^[a-z][a-z0-9-]{0,62}$"));
    if (!nodePattern.match(nodeName.trimmed()).hasMatch()) {
        return QStringLiteral(
            "The node name must start with a letter and contain only lowercase "
            "letters, digits, or hyphens (63 characters maximum).");
    }
    return {};
}

QJsonObject cheapestVultrPlan(const QJsonArray &plans)
{
    QJsonObject best;
    for (const QJsonValue &value : plans) {
        const QJsonObject plan = value.toObject();
        const double cost = plan.value(QStringLiteral("monthly_cost")).toDouble();
        const QString id = plan.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || !std::isfinite(cost) || cost <= 0.0 ||
            plan.value(QStringLiteral("locations")).toArray().isEmpty()) {
            continue;
        }
        if (best.isEmpty()) {
            best = plan;
            continue;
        }
        const double bestCost =
            best.value(QStringLiteral("monthly_cost")).toDouble();
        if (cost < bestCost) {
            best = plan;
            continue;
        }
        if (cost > bestCost)
            continue;
        const double ram = plan.value(QStringLiteral("ram")).toDouble();
        const double bestRam = best.value(QStringLiteral("ram")).toDouble();
        if (ram > bestRam ||
            (ram == bestRam &&
             id < best.value(QStringLiteral("id")).toString())) {
            best = plan;
        }
    }
    return best;
}

QString vultrPlanRegion(const QJsonObject &plan)
{
    QStringList locations;
    for (const QJsonValue &value :
         plan.value(QStringLiteral("locations")).toArray()) {
        const QString region = value.toString().trimmed();
        if (!region.isEmpty())
            locations.append(region);
    }
    std::sort(locations.begin(), locations.end());
    return locations.isEmpty() ? QString() : locations.first();
}

QJsonObject latestVultrDebianOs(const QJsonArray &osList)
{
    QJsonObject best;
    int bestVersion = -1;
    static const QRegularExpression versionPattern(
        QStringLiteral("\\b(\\d+)\\b"));
    for (const QJsonValue &value : osList) {
        const QJsonObject os = value.toObject();
        if (os.value(QStringLiteral("family")).toString().toLower() !=
                QLatin1String("debian") ||
            os.value(QStringLiteral("arch")).toString().toLower() !=
                QLatin1String("x64") ||
            os.value(QStringLiteral("id")).toInt() <= 0) {
            continue;
        }
        const QRegularExpressionMatch match =
            versionPattern.match(os.value(QStringLiteral("name")).toString());
        const int version = match.hasMatch() ? match.captured(1).toInt() : 0;
        if (version > bestVersion ||
            (version == bestVersion &&
             os.value(QStringLiteral("id")).toInt() >
                 best.value(QStringLiteral("id")).toInt())) {
            best = os;
            bestVersion = version;
        }
    }
    return best;
}

QJsonObject vultrInstanceCreatePayload(const QString &nodeName,
                                       const QString &planId,
                                       const QString &regionId,
                                       int osId,
                                       const QString &sshKeyId)
{
    return {
        {QStringLiteral("region"), regionId},
        {QStringLiteral("plan"), planId},
        {QStringLiteral("os_id"), osId},
        {QStringLiteral("label"), nodeName.trimmed()},
        {QStringLiteral("hostname"), nodeName.trimmed()},
        {QStringLiteral("sshkey_id"), QJsonArray{sshKeyId}},
        {QStringLiteral("backups"), QStringLiteral("disabled")},
        {QStringLiteral("activation_email"), false},
        {QStringLiteral("tags"),
         QJsonArray{QStringLiteral("forkmesh-mirror")}},
    };
}

QString vultrInstanceReadyIp(const QJsonObject &instance)
{
    if (instance.value(QStringLiteral("status")).toString() !=
            QLatin1String("active") ||
        instance.value(QStringLiteral("power_status")).toString() !=
            QLatin1String("running")) {
        return {};
    }
    const QString ip =
        instance.value(QStringLiteral("main_ip")).toString().trimmed();
    static const QRegularExpression ipv4Pattern(
        QStringLiteral("^(?:\\d{1,3}\\.){3}\\d{1,3}$"));
    if (ip.isEmpty() || ip == QLatin1String("0.0.0.0") ||
        !ipv4Pattern.match(ip).hasMatch()) {
        return {};
    }
    return ip;
}

QString vultrApiKeyFromVariables(const QMap<QString, QString> &variables)
{
    return storedCredential(variables, {
                                           QStringLiteral("VULTR_API_KEY"),
                                           QStringLiteral("VULTR_TOKEN"),
                                           QStringLiteral("VULTR_KEY"),
                                       });
}

QString cloudflareZoneNameFromVariables(
    const QMap<QString, QString> &variables)
{
    return storedCredential(variables,
                            {
                                QStringLiteral("CLOUDFLARE_ZONE"),
                                QStringLiteral("CLOUDFLARE_ZONE_NAME"),
                                QStringLiteral("CF_ZONE"),
                            });
}

QString vultrMirrorDnsHostname(const QString &nodeName,
                               const QString &zoneName)
{
    static const QRegularExpression labelPattern(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    static const QRegularExpression zonePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
                       "(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+$"));
    const QString node = nodeName.trimmed().toLower();
    QString zone = zoneName.trimmed().toLower();
    while (zone.endsWith(QLatin1Char('.')))
        zone.chop(1);
    if (!labelPattern.match(node).hasMatch() ||
        !zonePattern.match(zone).hasMatch()) {
        return {};
    }
    const QString hostname = node + QLatin1Char('.') + zone;
    return hostname.size() <= 253 ? hostname : QString();
}

QJsonObject vultrMirrorDnsRecordPayload(const QString &hostname,
                                        const QString &ip)
{
    const QString name = hostname.trimmed().toLower();
    const QString address = ip.trimmed();
    static const QRegularExpression namePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
                       "(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+$"));
    if (!namePattern.match(name).hasMatch() || name.size() > 253)
        return {};
    const QStringList octets = address.split(QLatin1Char('.'));
    if (octets.size() != 4 || address == QLatin1String("0.0.0.0"))
        return {};
    for (const QString &octet : octets) {
        bool ok = false;
        const int value = octet.toInt(&ok);
        if (!ok || octet.isEmpty() || octet.size() > 3 || value < 0 ||
            value > 255) {
            return {};
        }
    }
    return {
        {QStringLiteral("type"), QStringLiteral("A")},
        {QStringLiteral("name"), name},
        {QStringLiteral("content"), address},
        // DNS-only: the node is reached over SSH and its own listeners, and a
        // proxied answer would break both. The direct HTTPS mirror endpoint
        // keeps its own proxied Tunnel record.
        {QStringLiteral("proxied"), false},
        {QStringLiteral("ttl"), 1},
        {QStringLiteral("comment"), QStringLiteral("ForkMesh mirror node")},
    };
}

QString cloudflareZoneId(const QJsonArray &zones, const QString &zoneName)
{
    QString zone = zoneName.trimmed().toLower();
    while (zone.endsWith(QLatin1Char('.')))
        zone.chop(1);
    if (zone.isEmpty())
        return {};
    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    QString found;
    for (const QJsonValue &value : zones) {
        const QJsonObject candidate = value.toObject();
        if (candidate.value(QStringLiteral("name")).toString().trimmed().toLower() !=
            zone) {
            continue;
        }
        const QString id = candidate.value(QStringLiteral("id")).toString().trimmed();
        if (!idPattern.match(id).hasMatch())
            return {};
        if (!found.isEmpty() && found != id)
            return {};
        found = id;
    }
    return found;
}

QString cloudflareDnsRecordId(const QJsonArray &records,
                              const QString &hostname,
                              const QString &recordType)
{
    const QString name = hostname.trimmed().toLower();
    const QString type = recordType.trimmed().toUpper();
    if (name.isEmpty() || type.isEmpty())
        return {};
    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    QString found;
    for (const QJsonValue &value : records) {
        const QJsonObject record = value.toObject();
        if (record.value(QStringLiteral("name")).toString().trimmed().toLower() !=
                name ||
            record.value(QStringLiteral("type")).toString().trimmed().toUpper() !=
                type) {
            continue;
        }
        const QString id = record.value(QStringLiteral("id")).toString().trimmed();
        if (!idPattern.match(id).hasMatch())
            return {};
        if (!found.isEmpty() && found != id)
            return {};
        found = id;
    }
    return found;
}

} // namespace forkmesh::control
