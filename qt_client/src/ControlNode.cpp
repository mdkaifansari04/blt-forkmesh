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

// POSIX single-quoting: the only way a value can leave a shell word here.
QString shellSingleQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

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

HostSshCommand buildHostInteractiveSshCommand(const QString &host,
                                              const QString &sshUser,
                                              const QString &sshPassword,
                                              const QString &remoteCommand,
                                              QString *error,
                                              const QString &identityFile)
{
    HostSshCommand command = buildHostSshCommand(host, sshUser, sshPassword,
                                                 remoteCommand, error,
                                                 identityFile);
    if (command.program.isEmpty())
        return command;
    // The last two words are always "user@host" and the remote command, and
    // -tt has to precede both.
    command.arguments.insert(command.arguments.size() - 2,
                             QStringLiteral("-tt"));
    return command;
}

QString hostSshCommandLine(const HostSshCommand &command)
{
    if (command.program.isEmpty())
        return {};
    const auto quote = [](const QString &word) {
        QString value = word;
        value.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        return QLatin1Char('\'') + value + QLatin1Char('\'');
    };
    QStringList words{quote(command.program)};
    for (const QString &argument : command.arguments)
        words << quote(argument);
    return words.join(QLatin1Char(' '));
}

QString buildHostAgentLoginRemoteCommand()
{
    return QStringLiteral(
        "sh -lc 'if test \"$(id -u)\" = 0 && id forkmesh-node "
        ">/dev/null 2>&1; then "
        "echo \"Opening the forkmesh-node service account shell.\"; "
        "exec su -s /bin/bash forkmesh-node; fi; "
        "export PATH=\"$HOME/.local/bin:$HOME/.claude/bin:$PATH\"; "
        "echo \"Finish the provider sign-ins on this mirror:\"; "
        "echo \"  claude        then run /login inside it\"; "
        "echo \"  codex login\"; "
        "echo; "
        "exec \"${SHELL:-/bin/sh}\" -l'");
}

QString savedHostCredentialKey(const QString &nodeName, const QString &host,
                               const QString &sshUser)
{
    const QJsonArray identity{
        nodeName.trimmed(), host.trimmed().toLower(), sshUser.trimmed()};
    return QString::fromUtf8(
        QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

QString normalizeRemoteDiskPath(const QString &path)
{
    QString value = path.trimmed();
    if (value.isEmpty())
        value = QStringLiteral("/");
    if (!value.startsWith(QLatin1Char('/')) || value.contains(QChar::Null) ||
        value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')) ||
        value.size() > 4096) {
        return {};
    }
    QStringList parts;
    const QStringList raw = value.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : raw) {
        if (part == QStringLiteral("."))
            continue;
        if (part == QStringLiteral("..")) {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts.append(part);
    }
    if (parts.isEmpty())
        return QStringLiteral("/");
    return QLatin1Char('/') + parts.join(QLatin1Char('/'));
}

QString buildHostDiskUsageCommand(const QString &path, QString *error)
{
    const QString target = normalizeRemoteDiskPath(path);
    if (target.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "That is not a valid absolute path on the host.");
        }
        return {};
    }
    // Both failure messages are encoded here rather than on the host: the
    // remote side may have no base64 at all, and pre-encoding keeps the
    // sentinel grammar identical for every outcome.
    const auto sentinelText = [](const char *message) {
        return QString::fromLatin1(
            QByteArray(message).toBase64(QByteArray::Base64Encoding));
    };
    const QString unreadable = sentinelText(
        "ForkMesh could not read that directory on this host.");
    const QString noBase64 = sentinelText(
        "This host has no base64 command, so ForkMesh cannot read its size "
        "map safely.");
    // Read-only by construction: one `du` over a single directory level, with
    // every name handed back base64-encoded so it never becomes shell syntax.
    const QString script =
        QStringLiteral(
            "set -u\n"
            "LC_ALL=C\n"
            "export LC_ALL\n"
            "p=%1\n"
            "if [ ! -d \"$p\" ] || [ ! -r \"$p\" ]; then\n"
            "  printf 'FORKMESH-DU1-ERROR %2\\n'\n"
            "  exit 0\n"
            "fi\n"
            "if ! command -v base64 >/dev/null 2>&1; then\n"
            "  printf 'FORKMESH-DU1-ERROR %3\\n'\n"
            "  exit 0\n"
            "fi\n"
            "tab=$(printf '\\t')\n"
            "du -x -k -a -d 1 -- \"$p\" 2>/dev/null | "
            "while IFS=\"$tab\" read -r sz nm; do\n"
            "  [ -n \"${sz:-}\" ] || continue\n"
            "  if [ \"$nm\" = \"$p\" ]; then t=T\n"
            "  elif [ -L \"$nm\" ]; then t=f\n"
            "  elif [ -d \"$nm\" ]; then t=d\n"
            "  else t=f\n"
            "  fi\n"
            "  printf 'FORKMESH-DU1 %s %s %s\\n' \"$t\" \"$sz\" "
            "\"$(printf '%s' \"$nm\" | base64 | tr -d '\\n')\"\n"
            "done\n"
            "printf 'FORKMESH-DU1-END\\n'\n")
            .arg(shellSingleQuote(target), unreadable, noBase64);
    if (error)
        error->clear();
    return QStringLiteral("sh -lc ") + shellSingleQuote(script);
}

QString buildHostMountUsageCommand()
{
    const auto sentinelText = [](const char *message) {
        return QString::fromLatin1(
            QByteArray(message).toBase64(QByteArray::Base64Encoding));
    };
    const QString noBase64 = sentinelText(
        "This host has no base64 command, so ForkMesh cannot list its mount "
        "points safely.");
    const QString noDf = sentinelText(
        "This host has no df command, so ForkMesh cannot read mount usage.");
    // `df -P` guarantees one filesystem per logical record. The final field
    // may contain spaces, so reconstruct it after shifting the five fixed
    // fields. Only numeric capacity values and a base64 path cross the
    // sentinel boundary.
    const QString script =
        QStringLiteral(
            "set -u\n"
            "LC_ALL=C\n"
            "export LC_ALL\n"
            "if ! command -v base64 >/dev/null 2>&1; then\n"
            "  printf 'FORKMESH-MOUNT1-ERROR %1\\n'\n"
            "  exit 0\n"
            "fi\n"
            "if ! command -v df >/dev/null 2>&1; then\n"
            "  printf 'FORKMESH-MOUNT1-ERROR %2\\n'\n"
            "  exit 0\n"
            "fi\n"
            "df -P -k -l 2>/dev/null | sed 1d | "
            "while IFS= read -r line; do\n"
            "  set -- $line\n"
            "  [ \"$#\" -ge 6 ] || continue\n"
            "  total=$2; used=$3; avail=$4\n"
            "  shift 5\n"
            "  mp=$*\n"
            "  case \"$total:$used:$avail\" in\n"
            "    *[!0-9:]*) continue ;;\n"
            "  esac\n"
            "  case \"$mp\" in\n"
            "    /*) ;;\n"
            "    *) continue ;;\n"
            "  esac\n"
            "  printf 'FORKMESH-MOUNT1 %s %s %s %s\\n' "
            "\"$total\" \"$used\" \"$avail\" "
            "\"$(printf '%s' \"$mp\" | base64 | tr -d '\\n')\"\n"
            "done\n"
            "printf 'FORKMESH-MOUNT1-END\\n'\n")
            .arg(noBase64, noDf);
    return QStringLiteral("sh -lc ") + shellSingleQuote(script);
}

HostMountUsageList parseHostMountUsage(const QByteArray &output)
{
    HostMountUsageList result;
    QSet<QString> seenPaths;
    const QStringList lines =
        QString::fromUtf8(output)
            .split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                   Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line == QStringLiteral("FORKMESH-MOUNT1-END")) {
            result.complete = true;
            continue;
        }
        if (line.startsWith(QStringLiteral("FORKMESH-MOUNT1-ERROR "))) {
            const QByteArray decoded = QByteArray::fromBase64(
                line.mid(22).trimmed().toLatin1());
            result.error =
                decoded.isEmpty()
                    ? QStringLiteral("The host refused the mount-usage read.")
                    : QString::fromUtf8(decoded);
            result.complete = true;
            continue;
        }
        if (!line.startsWith(QStringLiteral("FORKMESH-MOUNT1 ")))
            continue;
        const QStringList fields =
            line.mid(16).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() != 4)
            continue;
        bool totalOk = false;
        bool usedOk = false;
        bool availableOk = false;
        const qint64 totalKib = fields.at(0).toLongLong(&totalOk);
        const qint64 usedKib = fields.at(1).toLongLong(&usedOk);
        const qint64 availableKib = fields.at(2).toLongLong(&availableOk);
        const QString path = normalizeRemoteDiskPath(QString::fromUtf8(
            QByteArray::fromBase64(fields.at(3).toLatin1())));
        if (!totalOk || !usedOk || !availableOk || totalKib <= 0 ||
            usedKib < 0 || availableKib < 0 || path.isEmpty() ||
            seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);
        result.mounts.append(
            HostMountUsage{path, totalKib * 1024, usedKib * 1024,
                           availableKib * 1024});
    }
    std::sort(result.mounts.begin(), result.mounts.end(),
              [](const HostMountUsage &a, const HostMountUsage &b) {
                  if (a.path == QStringLiteral("/"))
                      return b.path != QStringLiteral("/");
                  if (b.path == QStringLiteral("/"))
                      return false;
                  return a.path.localeAwareCompare(b.path) < 0;
              });
    return result;
}

HostDiskUsage parseHostDiskUsage(const QByteArray &output, const QString &path)
{
    HostDiskUsage usage;
    usage.path = normalizeRemoteDiskPath(path);
    const QStringList lines =
        QString::fromUtf8(output)
            .split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                   Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line == QStringLiteral("FORKMESH-DU1-END")) {
            usage.complete = true;
            continue;
        }
        if (line.startsWith(QStringLiteral("FORKMESH-DU1-ERROR "))) {
            const QByteArray decoded = QByteArray::fromBase64(
                line.mid(19).trimmed().toLatin1());
            usage.error = decoded.isEmpty()
                ? QStringLiteral("The host refused the size-map read.")
                : QString::fromUtf8(decoded);
            usage.complete = true;
            continue;
        }
        if (!line.startsWith(QStringLiteral("FORKMESH-DU1 ")))
            continue;
        const QStringList fields =
            line.mid(13).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() != 3)
            continue;
        bool ok = false;
        const qint64 kib = fields.at(1).toLongLong(&ok);
        if (!ok || kib < 0)
            continue;
        const QString decoded =
            QString::fromUtf8(QByteArray::fromBase64(fields.at(2).toLatin1()));
        if (decoded.isEmpty() || decoded.contains(QChar::Null))
            continue;
        const qint64 bytes = kib * 1024;
        if (fields.at(0) == QStringLiteral("T")) {
            usage.totalBytes = bytes;
            continue;
        }
        HostDiskEntry entry;
        entry.path = decoded;
        entry.name = decoded.section(QLatin1Char('/'), -1);
        if (entry.name.isEmpty())
            entry.name = decoded;
        entry.bytes = bytes;
        entry.directory = fields.at(0) == QStringLiteral("d");
        usage.entries.append(entry);
    }
    std::sort(usage.entries.begin(), usage.entries.end(),
              [](const HostDiskEntry &a, const HostDiskEntry &b) {
                  if (a.bytes != b.bytes)
                      return a.bytes > b.bytes;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    return usage;
}

QString formatDiskSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    static const char *const units[] = {"KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', value >= 100.0 ? 0 : 1)
        .arg(QLatin1String(units[unit]));
}

QString nonRoutableAddressNote(const QString &host)
{
    static const QRegularExpression ipv4Pattern(
        QStringLiteral("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$"));
    const QRegularExpressionMatch match = ipv4Pattern.match(host.trimmed());
    if (!match.hasMatch())
        return {};
    int octet[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        octet[i] = match.captured(i + 1).toInt();
        if (octet[i] > 255)
            return {};
    }
    if (octet[0] == 10)
        return QStringLiteral("the private range 10.0.0.0/8 (RFC 1918)");
    if (octet[0] == 172 && octet[1] >= 16 && octet[1] <= 31)
        return QStringLiteral("the private range 172.16.0.0/12 (RFC 1918)");
    if (octet[0] == 192 && octet[1] == 168)
        return QStringLiteral("the private range 192.168.0.0/16 (RFC 1918)");
    if (octet[0] == 100 && octet[1] >= 64 && octet[1] <= 127)
        return QStringLiteral(
            "100.64.0.0/10, the carrier-grade NAT / shared address range "
            "(RFC 6598) that VPN meshes such as Tailscale also hand out");
    if (octet[0] == 169 && octet[1] == 254)
        return QStringLiteral("the link-local range 169.254.0.0/16");
    if (octet[0] == 127)
        return QStringLiteral("the loopback range 127.0.0.0/8");
    return {};
}

QString sshFailureSummary(int exitCode, const QString &outputTail)
{
    QString reason;
    const QStringList lines =
        outputTail.split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                         Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        reason = line;
        break;
    }
    if (reason.size() > 160)
        reason = reason.left(157) + QStringLiteral("...");
    if (reason.isEmpty())
        return QStringLiteral("exit %1").arg(exitCode);
    return QString::fromUtf8("exit %1 \xE2\x80\x94 %2")
        .arg(QString::number(exitCode), reason);
}

QString sshConnectionFailureHint(int exitCode, const QString &outputTail,
                                 const QString &host)
{
    if (exitCode != 255)
        return {};
    const QString tail = outputTail.toLower();
    const bool dropped =
        tail.contains(QStringLiteral("connection timed out")) ||
        tail.contains(QStringLiteral("operation timed out")) ||
        tail.contains(QStringLiteral("no route to host"));
    // Packets vanishing towards an address that is not routable on the public
    // internet is not a firewall at all — no network between here and there
    // can carry them (adhoc #342). Say so instead of sending the operator off
    // to audit security groups that were never involved.
    if (dropped) {
        const QString range = nonRoutableAddressNote(host);
        if (!range.isEmpty())
            return QString::fromUtf8(
                       "%1 is in %2, so it is not reachable from the public "
                       "internet \xE2\x80\x94 the packets are dropped in "
                       "transit rather than by any firewall. Unless this "
                       "machine is on that same private network or VPN, use "
                       "the host's public address here (or connect to the "
                       "network that owns the range first).")
                .arg(host.trimmed(), range);
    }
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

bool sshFailureNeedsPassword(int exitCode, const QString &outputTail)
{
    if (exitCode != 255)
        return false;
    const QString tail = outputTail.toLower();
    return tail.contains(QStringLiteral("permission denied")) ||
           tail.contains(QStringLiteral("authentication failed")) ||
           tail.contains(
               QStringLiteral("no supported authentication methods"));
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

QString findSiteDeployScript(const QString &sourceDir,
                             const QString &applicationDir)
{
    QStringList candidates;
    const QString overridePath =
        qEnvironmentVariable("FORKMESH_SITE_DEPLOY").trimmed();
    if (!overridePath.isEmpty())
        candidates.append(overridePath);
    const QString worker =
        findCloudflareWorkerDirectory(sourceDir, applicationDir);
    if (!worker.isEmpty()) {
        candidates.append(
            QDir(worker).absoluteFilePath(QStringLiteral("deploy.sh")));
    }
    for (const QString &candidate : candidates) {
        const QString resolved = canonicalCandidate(candidate);
        if (!resolved.isEmpty())
            return resolved;
    }
    return {};
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

QString findMcpServerScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("forkmesh_mcp_server.py"),
        QStringLiteral("FORKMESH_MCP_SERVER"),
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

bool vultrPlanHasIpv4(const QJsonObject &plan)
{
    // Vultr marks its IPv6-only tiers with a "-v6" id suffix ("vc2-1c-0.5gb-v6")
    // and nothing else in the plan object distinguishes them.
    const QString id =
        plan.value(QStringLiteral("id")).toString().trimmed().toLower();
    if (id.isEmpty())
        return false;
    return !id.endsWith(QLatin1String("-v6")) &&
           !id.contains(QLatin1String("-v6-"));
}

QJsonObject cheapestVultrPlan(const QJsonArray &plans)
{
    // ForkMesh keeps the authenticated public repository materialization in
    // private temporary storage while the durable copy remains age-encrypted.
    // Vultr's 512 MB plans mount /tmp at roughly half of RAM, which is smaller
    // than the flagship repository and makes an otherwise successful install
    // disappear during its first sync. One GiB is the minimum supported
    // automatic mirror size; operators can still install manually on custom
    // hosts whose temporary-storage layout meets the same runtime needs.
    constexpr double kMinimumMirrorRamMb = 1024.0;
    const auto hasUsLocation = [](const QJsonObject &plan) {
        static const QSet<QString> usRegions{
            QStringLiteral("ewr"), // Newark, New Jersey / New York metro
            QStringLiteral("atl"), // Atlanta
            QStringLiteral("ord"), // Chicago
            QStringLiteral("dfw"), // Dallas
            QStringLiteral("mia"), // Miami
            QStringLiteral("lax"), // Los Angeles
            QStringLiteral("sea"), // Seattle
            QStringLiteral("sjc"), // Silicon Valley
            QStringLiteral("hon"), // Honolulu
        };
        for (const QJsonValue &value :
             plan.value(QStringLiteral("locations")).toArray()) {
            if (usRegions.contains(value.toString().trimmed().toLower()))
                return true;
        }
        return false;
    };
    QJsonObject best;
    for (const QJsonValue &value : plans) {
        const QJsonObject plan = value.toObject();
        const double cost = plan.value(QStringLiteral("monthly_cost")).toDouble();
        const double ram = plan.value(QStringLiteral("ram")).toDouble();
        const QString id = plan.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || !std::isfinite(cost) || cost <= 0.0 ||
            !std::isfinite(ram) || ram < kMinimumMirrorRamMb ||
            !vultrPlanHasIpv4(plan) || !hasUsLocation(plan)) {
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
    // Keep automatically provisioned World mirrors in the United States.
    // Newark is the closest Vultr region to New York City, followed by
    // Atlanta; the remaining US locations provide deterministic capacity
    // fallbacks without silently placing a mirror on another continent.
    static const QStringList preferredUsRegions{
        QStringLiteral("ewr"),
        QStringLiteral("atl"),
        QStringLiteral("ord"),
        QStringLiteral("dfw"),
        QStringLiteral("mia"),
        QStringLiteral("lax"),
        QStringLiteral("sea"),
        QStringLiteral("sjc"),
        QStringLiteral("hon"),
    };
    QSet<QString> locations;
    for (const QJsonValue &value :
         plan.value(QStringLiteral("locations")).toArray()) {
        const QString region = value.toString().trimmed().toLower();
        if (!region.isEmpty())
            locations.insert(region);
    }
    for (const QString &region : preferredUsRegions) {
        if (locations.contains(region))
            return region;
    }
    return {};
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

bool localBinaryRunsOnVultrMirror(const QString &kernelType,
                                  const QString &cpuArch)
{
    // The installer accepts an uploaded binary only when the uploader's
    // declared OS/arch match the target, and every mirror we create is x64
    // Debian — so an upload is worth streaming exactly when this app is a
    // linux/x86_64 build. Anything else (macOS, Windows, arm64) would be
    // rejected remotely and fall back to the relay download anyway.
    const QString os = kernelType.trimmed().toLower();
    QString arch = cpuArch.trimmed().toLower();
    if (arch == QLatin1String("amd64") || arch == QLatin1String("x64"))
        arch = QStringLiteral("x86_64");
    return os == QLatin1String("linux") && arch == QLatin1String("x86_64");
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
        // The mesh reaches mirrors over IPv4 only: never let Vultr hand back an
        // instance whose sole address is a v6 one (adhoc #344).
        {QStringLiteral("enable_ipv6"), false},
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

bool vultrInstanceIsIpv6Only(const QJsonObject &instance)
{
    const QString v6 =
        instance.value(QStringLiteral("v6_main_ip")).toString().trimmed();
    if (v6.isEmpty() || !v6.contains(QLatin1Char(':')))
        return false;
    const QString ip =
        instance.value(QStringLiteral("main_ip")).toString().trimmed();
    return ip.isEmpty() || ip == QLatin1String("0.0.0.0");
}

QString nextMirrorNodeName(const QStringList &existingNames)
{
    static const QRegularExpression mirrorPattern(
        QStringLiteral("^mirror-?(\\d{1,4})$"));
    QSet<QString> used;
    int highest = 0;
    for (const QString &name : existingNames) {
        const QString normalized = name.trimmed().toLower();
        if (normalized.isEmpty())
            continue;
        used.insert(normalized);
        const QRegularExpressionMatch match = mirrorPattern.match(normalized);
        if (match.hasMatch())
            highest = std::max(highest, match.captured(1).toInt());
    }
    // Continue the fleet's own numbering (mirror1..mirror4 -> mirror5) and then
    // walk forward past any name already taken, so the default never collides.
    for (int i = std::max(1, highest + 1); i <= 9999; ++i) {
        const QString candidate = QStringLiteral("mirror%1").arg(i);
        if (!used.contains(candidate))
            return candidate;
    }
    return {};
}

QString vultrApiKeyFromVariables(const QMap<QString, QString> &variables)
{
    return storedCredential(variables, {
                                           QStringLiteral("VULTR_API_KEY"),
                                           QStringLiteral("VULTR_TOKEN"),
                                           QStringLiteral("VULTR_KEY"),
                                       });
}

bool vultrInstallNeedsLocalBinary(const QString &installOutput)
{
    // Matched on the installer's own wording for the two dead ends a retry
    // cannot clear: nothing in the mesh is serving the repo, and no prebuilt
    // release exists (or authenticates) for the instance's platform.
    static const QStringList markers = {
        QStringLiteral("No online ForkMesh node"),
        QStringLiteral("No prebuilt ForkMesh binary is published"),
        QStringLiteral("No prebuilt ForkMesh release passed"),
        QStringLiteral("falling back to a source build is disabled"),
    };
    for (const QString &marker : markers) {
        if (installOutput.contains(marker))
            return true;
    }
    return false;
}

QString savedHostVultrInstanceId(const QJsonObject &host)
{
    const QString provider =
        host.value(QStringLiteral("provider")).toString().trimmed();
    if (provider.compare(QStringLiteral("Vultr"), Qt::CaseInsensitive) != 0)
        return {};
    return host.value(QStringLiteral("instanceId")).toString().trimmed();
}

QString vultrInstanceIdForAddress(const QJsonArray &instances,
                                  const QString &address)
{
    const QString wanted = address.trimmed().toLower();
    if (wanted.isEmpty())
        return {};
    QString match;
    for (const QJsonValue &value : instances) {
        const QJsonObject instance = value.toObject();
        const QString id = instance.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty())
            continue;
        bool hit = false;
        for (const QString &field : {QStringLiteral("main_ip"),
                                     QStringLiteral("v6_main_ip"),
                                     QStringLiteral("label"),
                                     QStringLiteral("hostname")}) {
            const QString candidate =
                instance.value(field).toString().trimmed().toLower();
            // Vultr reports an unassigned address as "0.0.0.0"/"", which would
            // otherwise let two booting instances "match" each other.
            if (candidate.isEmpty() ||
                candidate == QLatin1String("0.0.0.0")) {
                continue;
            }
            if (candidate == wanted) {
                hit = true;
                break;
            }
        }
        if (!hit)
            continue;
        if (!match.isEmpty() && match != id)
            return {}; // ambiguous — fail closed rather than destroy a guess
        match = id;
    }
    return match;
}

QString validateVultrDestroyRequest(const QString &apiKey,
                                    const QString &instanceId)
{
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9]{20,128}$"));
    if (!keyPattern.match(apiKey.trimmed()).hasMatch()) {
        return QStringLiteral(
            "Enter your Vultr API key (Account \xE2\x86\x92 API in the Vultr "
            "panel). It is used from memory only and never saved to disk.");
    }
    static const QRegularExpression idPattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
        "[0-9a-fA-F]{12}$"));
    if (!idPattern.match(instanceId.trimmed()).hasMatch()) {
        return QStringLiteral(
            "No Vultr instance is recorded for this host, so there is nothing "
            "safe to destroy. Delete it from the Vultr panel instead.");
    }
    return {};
}

bool agentCliCredentialsAreEmpty(const AgentCliCredentials &credentials)
{
    return credentials.claudeCredentials.trimmed().isEmpty() &&
           credentials.codexAuth.trimmed().isEmpty() &&
           agentCliEnvFileContents(credentials.env).isEmpty();
}

namespace {

// Environment variables are written into a sourced shell file, so only plain
// upper-case names with a printable single-line value are ever accepted.
bool agentCliEnvEntryIsUsable(const QString &name, const QString &value)
{
    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Z][A-Z0-9_]{0,63}$"));
    if (!namePattern.match(name).hasMatch())
        return false;
    if (value.isEmpty() || value.size() > 4096)
        return false;
    for (const QChar ch : value) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return false;
    }
    return true;
}

} // namespace

QString describeAgentCliCredentials(const AgentCliCredentials &credentials)
{
    QStringList parts;
    if (!credentials.claudeCredentials.trimmed().isEmpty())
        parts.append(QStringLiteral("Claude Code login"));
    if (!credentials.codexAuth.trimmed().isEmpty())
        parts.append(QStringLiteral("Codex login"));
    QStringList names;
    for (auto it = credentials.env.constBegin();
         it != credentials.env.constEnd(); ++it) {
        if (agentCliEnvEntryIsUsable(it.key(), it.value()))
            names.append(it.key());
    }
    names.sort();
    parts += names;
    return parts.join(QStringLiteral(", "));
}

QByteArray agentCliEnvFileContents(const QMap<QString, QString> &env)
{
    QByteArray contents;
    // QMap iterates in key order, so the file is byte-identical run to run.
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        if (!agentCliEnvEntryIsUsable(it.key(), it.value()))
            continue;
        // Single-quoted, so nothing in a value can be read as shell syntax.
        contents += QStringLiteral("export %1=%2\n")
                        .arg(it.key(), shellSingleQuote(it.value()))
                        .toUtf8();
    }
    return contents;
}

QByteArray buildAgentCliBootstrapPayload(const AgentCliCredentials &credentials,
                                         QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    constexpr int kMaxSectionBytes = 256 * 1024;
    QByteArray payload;
    const auto appendSection = [&payload](const char *name,
                                          const QByteArray &data) {
        payload += QByteArray(name) + ' ' + data.toBase64() + '\n';
    };
    const QByteArray claude = credentials.claudeCredentials.trimmed();
    if (!claude.isEmpty()) {
        if (claude.size() > kMaxSectionBytes ||
            !QJsonDocument::fromJson(claude).isObject()) {
            return fail(QStringLiteral(
                "This device's Claude Code login file is not readable as a "
                "credential document."));
        }
        appendSection("claude", claude);
    }
    const QByteArray codex = credentials.codexAuth.trimmed();
    if (!codex.isEmpty()) {
        if (codex.size() > kMaxSectionBytes ||
            !QJsonDocument::fromJson(codex).isObject()) {
            return fail(QStringLiteral(
                "This device's Codex login file is not readable as a "
                "credential document."));
        }
        appendSection("codex", codex);
    }
    const QByteArray env = agentCliEnvFileContents(credentials.env);
    if (!env.isEmpty())
        appendSection("env", env);
    if (payload.isEmpty()) {
        return fail(QStringLiteral(
            "This device has no Claude Code or Codex login to copy."));
    }
    if (error)
        error->clear();
    return payload;
}

QString agentCliBootstrapRemoteCommand(bool withCredentials)
{
    // One fixed, secret-free command. The credential variant reads the stdin
    // payload into a private temp file *first*, so stdin is at EOF before the
    // piped installers run and no section can be mistaken for installer input.
    QStringList script;
    script << QStringLiteral("set -eu")
           << QStringLiteral("umask 077")
           << QStringLiteral("home=${HOME:-/root}");
    if (withCredentials) {
        script << QStringLiteral("tmp=$(mktemp)")
               << QStringLiteral("cat > \"$tmp\"")
               << QStringLiteral(
                      "sec() { sed -n \"s/^$1 //p\" \"$tmp\" | base64 -d; }")
               << QStringLiteral("has() { grep -q \"^$1 \" \"$tmp\"; }");
    }
    script << QStringLiteral("echo \"Installing Claude Code from claude.ai...\"")
           << QStringLiteral("curl -fsSL https://claude.ai/install.sh | bash")
           << QStringLiteral(
                  "echo \"Installing Codex from chatgpt.com...\"")
           << QStringLiteral(
                  "curl -fsSL https://chatgpt.com/codex/install.sh | sh")
           << QStringLiteral(
                  "export PATH=\"$home/.local/bin:$home/.claude/bin:$PATH\"")
           // Managed headless nodes run as forkmesh-node, not as the root SSH
           // provisioner. Install immutable global copies for that service and
           // direct copied login files into its private state home.
           << QStringLiteral("agent_home=\"$home\"")
           << QStringLiteral("agent_owner=\"\"")
           << QStringLiteral(
                  "if test \"$(id -u)\" = 0 && id forkmesh-node "
                  ">/dev/null 2>&1; then "
                  "agent_home=$(getent passwd forkmesh-node | cut -d: -f6); "
                  "agent_owner=forkmesh-node; "
                  "for program in claude codex; do "
                  "source_path=$(command -v \"$program\"); "
                  "install -m 0755 \"$(readlink -f \"$source_path\")\" "
                  "\"/usr/local/bin/$program\"; done; "
                  "echo \"Installed agent CLIs for the forkmesh-node service.\"; "
                  "fi");
    if (withCredentials) {
        script << QStringLiteral("if has claude; then mkdir -p \"$agent_home/.claude\"; "
                                 "sec claude > \"$agent_home/.claude/.credentials.json\"; "
                                 "chmod 600 \"$agent_home/.claude/.credentials.json\"; "
                                 "echo \"Copied the controller Claude Code login.\"; fi")
               << QStringLiteral("if has codex; then mkdir -p \"$agent_home/.codex\"; "
                                 "sec codex > \"$agent_home/.codex/auth.json\"; "
                                 "chmod 600 \"$agent_home/.codex/auth.json\"; "
                                 "echo \"Copied the controller Codex login.\"; fi")
               // The API-key file is sourced from the shell startup files a
               // ForkMesh SSH session (sh -lc) and an interactive login both
               // read, so agent runs on this mirror inherit the keys.
               << QStringLiteral("if has env; then mkdir -p \"$agent_home/.forkmesh\"; "
                                 "sec env > \"$agent_home/.forkmesh/agent-env\"; "
                                 "chmod 600 \"$agent_home/.forkmesh/agent-env\"; "
                                 "for rc in \"$agent_home/.profile\" \"$agent_home/.bashrc\"; do "
                                 "touch \"$rc\"; "
                                 "grep -q .forkmesh/agent-env \"$rc\" || "
                                 "printf \"%s\\n\" \". \\\"$agent_home/.forkmesh/agent-env\\\"\" "
                                 ">> \"$rc\"; done; "
                                 "echo \"Installed the agent API keys in "
                                 "~/.forkmesh/agent-env.\"; fi")
               << QStringLiteral(
                      "if test -n \"$agent_owner\"; then "
                      "chown -R \"$agent_owner:$agent_owner\" "
                      "\"$agent_home/.claude\" \"$agent_home/.codex\" "
                      "\"$agent_home/.forkmesh\" "
                      "\"$agent_home/.profile\" \"$agent_home/.bashrc\" "
                      "2>/dev/null || true; fi")
               << QStringLiteral("rm -f \"$tmp\"");
    }
    script << QStringLiteral("echo \"Claude Code:\"")
           << QStringLiteral("command -v claude; claude --version")
           << QStringLiteral("echo \"Codex:\"")
           << QStringLiteral("command -v codex; codex --version")
           << (withCredentials
                   ? QStringLiteral(
                         "echo \"Installation complete. This mirror is signed "
                         "in with the controller agent access.\"")
                   : QStringLiteral(
                         "echo \"Installation complete. Provider login is "
                         "still required on this mirror.\""));
    return QStringLiteral("sh -lc '%1'")
        .arg(script.join(QStringLiteral("; ")));
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

// --- Cloudflare API token check and rotation (adhoc #108) ------------------

namespace {

const QRegularExpression &cloudflareIdPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9]{1,64}$"));
    return pattern;
}

// The scope string Cloudflare tags a permission group with, and the resource
// key prefix a policy for it must use.
QString cloudflareScopeString(const QString &scope)
{
    if (scope == QLatin1String("account"))
        return QStringLiteral("com.cloudflare.api.account");
    if (scope == QLatin1String("zone"))
        return QStringLiteral("com.cloudflare.api.account.zone");
    if (scope == QLatin1String("user"))
        return QStringLiteral("com.cloudflare.api.user");
    return {};
}

// Accept either a full Cloudflare envelope or the bare `result` object, so
// callers can hand over whatever they already have.
QJsonObject cloudflareResultObject(const QJsonObject &response)
{
    const QJsonValue result = response.value(QStringLiteral("result"));
    return result.isObject() ? result.toObject() : response;
}

// Every resource key a token's policies name, flattened over the nested form
// Cloudflare uses for "all zones in this account".
QStringList cloudflarePolicyResourceKeys(const QJsonObject &tokenDetail)
{
    QStringList keys;
    const QJsonArray policies =
        cloudflareResultObject(tokenDetail)
            .value(QStringLiteral("policies"))
            .toArray();
    for (const QJsonValue &value : policies) {
        const QJsonObject resources =
            value.toObject().value(QStringLiteral("resources")).toObject();
        for (auto it = resources.constBegin(); it != resources.constEnd();
             ++it) {
            keys.append(it.key());
            if (!it.value().isObject())
                continue;
            const QJsonObject nested = it.value().toObject();
            for (auto inner = nested.constBegin(); inner != nested.constEnd();
                 ++inner) {
                keys.append(inner.key());
            }
        }
    }
    keys.removeDuplicates();
    return keys;
}

// Resolve one permission group's id from GET /user/tokens/permission_groups.
// A name is only accepted when the catalog entry also carries the scope the
// requirement is bound to: several group names (e.g. "Logs Read") exist at both
// account and zone scope with different ids.
QString cloudflarePermissionGroupId(const QJsonArray &catalog,
                                    const QString &name, const QString &scope)
{
    const QString wantedScope = cloudflareScopeString(scope);
    if (wantedScope.isEmpty())
        return {};
    for (const QJsonValue &value : catalog) {
        const QJsonObject group = value.toObject();
        if (group.value(QStringLiteral("name"))
                .toString()
                .trimmed()
                .compare(name, Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool scoped = false;
        const QJsonArray scopes =
            group.value(QStringLiteral("scopes")).toArray();
        for (const QJsonValue &entry : scopes) {
            if (entry.toString().trimmed() == wantedScope) {
                scoped = true;
                break;
            }
        }
        if (!scoped)
            continue;
        const QString id =
            group.value(QStringLiteral("id")).toString().trimmed();
        if (cloudflareIdPattern().match(id).hasMatch())
            return id;
    }
    return {};
}

} // namespace

QList<CloudflareTokenRequirement> cloudflareTokenRequirements()
{
    return {
        {QStringLiteral("account_settings"),
         QStringLiteral("Account Settings: Read"),
         QStringLiteral("Discovers the account that owns the Worker, the D1 "
                        "database and the Tunnel."),
         {QStringLiteral("Account Settings Read"),
          QStringLiteral("Account Settings Write")},
         QStringLiteral("account"),
         QStringLiteral("/accounts?per_page=1"),
         true},
        {QStringLiteral("workers_scripts"),
         QStringLiteral("Workers Scripts: Edit"),
         QStringLiteral("Uploads the relay Worker with its static assets, "
                        "Durable Objects, cron triggers and secrets."),
         {QStringLiteral("Workers Scripts Write")},
         QStringLiteral("account"),
         QStringLiteral("/accounts/{account}/workers/scripts"),
         true},
        {QStringLiteral("d1"),
         QStringLiteral("D1: Edit"),
         QStringLiteral("Creates and migrates the forkmesh D1 database the "
                        "Worker binds as DB."),
         {QStringLiteral("D1 Write")},
         QStringLiteral("account"),
         QStringLiteral("/accounts/{account}/d1/database?per_page=1"),
         true},
        {QStringLiteral("zone"),
         QStringLiteral("Zone: Read"),
         QStringLiteral("Finds the zone the relay and mirror hostnames are "
                        "derived from."),
         {QStringLiteral("Zone Read"), QStringLiteral("Zone Write")},
         QStringLiteral("zone"),
         QStringLiteral("/zones?per_page=1"),
         true},
        {QStringLiteral("dns"),
         QStringLiteral("DNS: Edit"),
         QStringLiteral("Publishes the proxied relay and mirror records, plus "
                        "the A record of each one-click mirror."),
         {QStringLiteral("DNS Write")},
         QStringLiteral("zone"),
         QStringLiteral("/zones/{zone}/dns_records?per_page=1"),
         true},
        {QStringLiteral("tunnel"),
         QStringLiteral("Cloudflare Tunnel: Edit"),
         QStringLiteral("Provisions the direct-HTTPS mirror gateway's Tunnel "
                        "and its connector credential."),
         {QStringLiteral("Cloudflare Tunnel Write"),
          QStringLiteral("Argo Tunnel Write")},
         QStringLiteral("account"),
         QStringLiteral("/accounts/{account}/cfd_tunnel?per_page=1"),
         true},
        {QStringLiteral("workers_tail"),
         QStringLiteral("Workers Tail: Read"),
         QStringLiteral("Streams the live Worker log shown in Network > Logs."),
         {QStringLiteral("Workers Tail Read")},
         QStringLiteral("account"),
         QString(),
         false},
        {QStringLiteral("api_tokens_read"),
         QStringLiteral("API Tokens: Read"),
         QStringLiteral("Lets this page report the token's exact policy instead "
                        "of inferring it from read-only probes."),
         {QStringLiteral("API Tokens Read")},
         QStringLiteral("user"),
         QStringLiteral("/user/tokens?per_page=1"),
         false},
        {QStringLiteral("api_tokens_write"),
         QStringLiteral("API Tokens: Edit"),
         QStringLiteral("Required by this page's own button that mints a "
                        "replacement token."),
         {QStringLiteral("API Tokens Write")},
         QStringLiteral("user"),
         QString(),
         false},
    };
}

QString cloudflareTokenProbePath(const CloudflareTokenRequirement &requirement,
                                 const QString &accountId,
                                 const QString &zoneId)
{
    QString path = requirement.probePath;
    if (path.isEmpty())
        return {};
    const auto substitute = [&path](const QString &placeholder,
                                    const QString &id) {
        if (!path.contains(placeholder))
            return true;
        const QString value = id.trimmed();
        if (!cloudflareIdPattern().match(value).hasMatch())
            return false;
        path.replace(placeholder, value);
        return true;
    };
    if (!substitute(QStringLiteral("{account}"), accountId) ||
        !substitute(QStringLiteral("{zone}"), zoneId)) {
        return {};
    }
    return path;
}

bool isPlausibleCloudflareApiToken(const QString &token)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_-]{20,160}$"));
    return pattern.match(token).hasMatch();
}

QString cloudflareTokenVerifyStatus(const QJsonObject &verifyResult)
{
    return cloudflareResultObject(verifyResult)
        .value(QStringLiteral("status"))
        .toString()
        .trimmed()
        .toLower();
}

QString cloudflareTokenVerifyId(const QJsonObject &verifyResult)
{
    const QString id = cloudflareResultObject(verifyResult)
                           .value(QStringLiteral("id"))
                           .toString()
                           .trimmed();
    return cloudflareIdPattern().match(id).hasMatch() ? id : QString();
}

QStringList cloudflareTokenPermissionGroupNames(const QJsonObject &tokenDetail)
{
    QStringList names;
    const QJsonArray policies =
        cloudflareResultObject(tokenDetail)
            .value(QStringLiteral("policies"))
            .toArray();
    for (const QJsonValue &value : policies) {
        const QJsonObject policy = value.toObject();
        // A deny policy subtracts access; reporting its groups as granted would
        // be a false positive on exactly the permission that is missing.
        if (policy.value(QStringLiteral("effect")).toString().trimmed().toLower() ==
            QLatin1String("deny")) {
            continue;
        }
        const QJsonArray groups =
            policy.value(QStringLiteral("permission_groups")).toArray();
        for (const QJsonValue &entry : groups) {
            const QString name = entry.toObject()
                                     .value(QStringLiteral("name"))
                                     .toString()
                                     .trimmed();
            if (!name.isEmpty())
                names.append(name);
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool cloudflareTokenGrantsRequirement(
    const CloudflareTokenRequirement &requirement,
    const QStringList &grantedGroupNames)
{
    for (const QString &accepted : requirement.groupNames) {
        for (const QString &granted : grantedGroupNames) {
            if (granted.compare(accepted, Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}

QStringList cloudflareTokenAccountIds(const QJsonObject &tokenDetail)
{
    static const QRegularExpression accountKey(
        QStringLiteral("^com\\.cloudflare\\.api\\.account\\.([A-Za-z0-9]{1,64})$"));
    QStringList ids;
    const QStringList keys = cloudflarePolicyResourceKeys(tokenDetail);
    for (const QString &key : keys) {
        const QRegularExpressionMatch match = accountKey.match(key);
        if (match.hasMatch())
            ids.append(match.captured(1));
    }
    ids.removeDuplicates();
    return ids;
}

QString cloudflareTokenUserResourceKey(const QJsonObject &tokenDetail)
{
    static const QRegularExpression userKey(
        QStringLiteral("^com\\.cloudflare\\.api\\.user\\.[A-Za-z0-9]{1,64}$"));
    const QStringList keys = cloudflarePolicyResourceKeys(tokenDetail);
    for (const QString &key : keys) {
        if (userKey.match(key).hasMatch())
            return key;
    }
    return {};
}

QJsonObject cloudflareTokenCreatePayload(
    const QString &tokenName, const QString &accountId, const QString &zoneId,
    const QString &userResourceKey, const QJsonArray &permissionGroupCatalog,
    QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QJsonObject();
    };
    if (error)
        error->clear();

    const QString name = tokenName.trimmed();
    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._:-]{0,119}$"));
    if (!namePattern.match(name).hasMatch())
        return fail(QStringLiteral("The token name is not a valid Cloudflare "
                                   "token name."));
    const QString account = accountId.trimmed();
    const QString zone = zoneId.trimmed();
    const QString user = userResourceKey.trimmed();
    static const QRegularExpression userKey(
        QStringLiteral("^com\\.cloudflare\\.api\\.user\\.[A-Za-z0-9]{1,64}$"));
    if (!account.isEmpty() && !cloudflareIdPattern().match(account).hasMatch())
        return fail(QStringLiteral("The Cloudflare account ID is malformed."));
    if (!zone.isEmpty() && !cloudflareIdPattern().match(zone).hasMatch())
        return fail(QStringLiteral("The Cloudflare zone ID is malformed."));
    if (!user.isEmpty() && !userKey.match(user).hasMatch())
        return fail(QStringLiteral("The Cloudflare user resource is malformed."));

    QJsonArray accountGroups;
    QJsonArray zoneGroups;
    QJsonArray userGroups;
    const QList<CloudflareTokenRequirement> requirements =
        cloudflareTokenRequirements();
    for (const CloudflareTokenRequirement &requirement : requirements) {
        const bool haveResource =
            (requirement.scope == QLatin1String("account") && !account.isEmpty()) ||
            (requirement.scope == QLatin1String("zone") && !zone.isEmpty()) ||
            (requirement.scope == QLatin1String("user") && !user.isEmpty());
        QString groupId;
        QString groupName;
        if (haveResource) {
            for (const QString &candidate : requirement.groupNames) {
                groupId = cloudflarePermissionGroupId(permissionGroupCatalog,
                                                      candidate,
                                                      requirement.scope);
                if (!groupId.isEmpty()) {
                    groupName = candidate;
                    break;
                }
            }
        }
        if (groupId.isEmpty()) {
            // Optional capabilities are dropped rather than blocking rotation:
            // a token that cannot read its own policies still deploys.
            if (!requirement.required)
                continue;
            if (!haveResource) {
                return fail(
                    QStringLiteral("%1 needs a Cloudflare %2 ID; none is "
                                   "configured on this page.")
                        .arg(requirement.label, requirement.scope));
            }
            return fail(
                QStringLiteral("Cloudflare's permission-group catalog has no "
                               "%1 group for %2.")
                    .arg(requirement.groupNames.value(0), requirement.label));
        }
        const QJsonObject group{
            {QStringLiteral("id"), groupId},
            {QStringLiteral("name"), groupName},
        };
        if (requirement.scope == QLatin1String("account"))
            accountGroups.append(group);
        else if (requirement.scope == QLatin1String("zone"))
            zoneGroups.append(group);
        else
            userGroups.append(group);
    }

    QJsonArray policies;
    const auto addPolicy = [&policies](const QString &resourceKey,
                                       const QJsonArray &groups) {
        if (groups.isEmpty())
            return;
        policies.append(QJsonObject{
            {QStringLiteral("effect"), QStringLiteral("allow")},
            {QStringLiteral("resources"),
             QJsonObject{{resourceKey, QStringLiteral("*")}}},
            {QStringLiteral("permission_groups"), groups},
        });
    };
    addPolicy(QStringLiteral("com.cloudflare.api.account.") + account,
              accountGroups);
    addPolicy(QStringLiteral("com.cloudflare.api.account.zone.") + zone,
              zoneGroups);
    addPolicy(user, userGroups);
    if (policies.isEmpty())
        return fail(QStringLiteral("No Cloudflare permissions could be resolved "
                                   "for a replacement token."));
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("policies"), policies},
    };
}

QString cloudflareCreatedTokenValue(const QJsonObject &createResult)
{
    const QString value = cloudflareResultObject(createResult)
                              .value(QStringLiteral("value"))
                              .toString()
                              .trimmed();
    return isPlausibleCloudflareApiToken(value) ? value : QString();
}

QString updatedEnvAssignment(const QString &contents, const QString &name,
                             const QString &value)
{
    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,127}$"));
    if (!namePattern.match(name).hasMatch() ||
        value.contains(QLatin1Char('\n')) ||
        value.contains(QLatin1Char('\r'))) {
        return contents;
    }
    const QString assignment = name + QLatin1Char('=') + value;
    // Only a real assignment counts: a commented-out example keeps its place,
    // and deploy.sh ignores it too.
    const QRegularExpression linePattern(
        QStringLiteral("^[ \\t]*") + QRegularExpression::escape(name) +
        QStringLiteral("[ \\t]*="));

    QStringList lines = contents.split(QLatin1Char('\n'));
    bool replaced = false;
    for (int index = 0; index < lines.size();) {
        QString line = lines.at(index);
        const bool carriage = line.endsWith(QLatin1Char('\r'));
        if (carriage)
            line.chop(1);
        if (!linePattern.match(line).hasMatch()) {
            ++index;
            continue;
        }
        if (!replaced) {
            lines[index] = carriage ? assignment + QLatin1Char('\r')
                                    : assignment;
            replaced = true;
            ++index;
            continue;
        }
        // A later duplicate would win when deploy.sh sources the file, so a
        // rotation that left one behind would keep exporting the old token.
        lines.removeAt(index);
    }
    if (!replaced) {
        while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty())
            lines.removeLast();
        lines.append(assignment);
    }
    QString result = lines.join(QLatin1Char('\n'));
    if (!result.endsWith(QLatin1Char('\n')))
        result.append(QLatin1Char('\n'));
    return result;
}

QString siteDeployEnvFilePath(const QString &sourceDir,
                              const QString &applicationDir)
{
    const QString worker =
        findCloudflareWorkerDirectory(sourceDir, applicationDir);
    if (worker.isEmpty())
        return {};
    return QDir(worker).absoluteFilePath(QStringLiteral(".env.production"));
}

QString maskedTokenSuffix(const QString &token)
{
    const QString ellipsis = QString::fromUtf8("\xE2\x80\xA6");
    const QString trimmed = token.trimmed();
    return trimmed.size() < 8 ? ellipsis : ellipsis + trimmed.right(4);
}

} // namespace forkmesh::control
