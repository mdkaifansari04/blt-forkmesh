#include "RepoStatsStore.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>

namespace {
QJsonObject readDocument(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeDocument(const QString &path, const QJsonObject &object, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QByteArray git(const QString &repo, const QStringList &args, bool *ok = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(repo);
    process.start(QStringLiteral("git"), args);
    const bool finished = process.waitForFinished(30000);
    const bool success = finished && process.exitStatus() == QProcess::NormalExit &&
                         process.exitCode() == 0;
    if (ok) *ok = success;
    return success ? process.readAllStandardOutput() : QByteArray();
}
}

QString RepoStatsStore::statsPath(const QString &repoDir)
{
    return QDir(repoDir).filePath(QStringLiteral(".forkmesh/stats/repository.json"));
}

RepoStatsSample RepoStatsStore::measure(const QString &repoDir, QString *error)
{
    RepoStatsSample sample;
    sample.day = QDate::currentDate().toString(Qt::ISODate);
    bool ok = false;
    const QList<QByteArray> paths = git(repoDir, {QStringLiteral("ls-files"),
                                                 QStringLiteral("-z")}, &ok)
                                      .split('\0');
    if (!ok) {
        if (error) *error = QStringLiteral("git ls-files failed");
        return {};
    }
    for (const QByteArray &encoded : paths) {
        if (encoded.isEmpty()) continue;
        if (encoded.startsWith(".forkmesh/stats/")) continue;
        QFile file(QDir(repoDir).filePath(QString::fromUtf8(encoded)));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = file.readAll();
        sample.bytes += data.size();
        ++sample.files;
        if (!data.contains('\0')) {
            sample.lines += data.count('\n');
            if (!data.isEmpty() && !data.endsWith('\n')) ++sample.lines;
        }
    }
    return sample;
}

QVector<RepoStatsSample> RepoStatsStore::load(const QString &repoDir, QString *error)
{
    QVector<RepoStatsSample> result;
    const QJsonObject root = readDocument(statsPath(repoDir));
    for (const QJsonValue &value : root.value(QStringLiteral("days")).toArray()) {
        const QJsonObject item = value.toObject();
        RepoStatsSample sample;
        sample.day = item.value(QStringLiteral("day")).toString();
        sample.bytes = qint64(item.value(QStringLiteral("bytes")).toDouble());
        sample.lines = qint64(item.value(QStringLiteral("lines")).toDouble());
        sample.files = qint64(item.value(QStringLiteral("files")).toDouble());
        if (!sample.day.isEmpty()) result.append(sample);
    }
    if (result.size() > 30) result = result.mid(result.size() - 30);
    Q_UNUSED(error);
    return result;
}

QVector<RepoStatsSample> RepoStatsStore::captureDaily(const QString &repoDir,
                                                       QString *error)
{
    QVector<RepoStatsSample> days = load(repoDir, error);
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (!days.isEmpty() && days.last().day == today)
        return days;
    RepoStatsSample current = measure(repoDir, error);
    if (current.day.isEmpty()) return days;
    QJsonObject root = readDocument(statsPath(repoDir));
    days.append(current);
    while (days.size() > 30) days.removeFirst();
    QJsonArray encoded;
    for (const RepoStatsSample &sample : days)
        encoded.append(QJsonObject{{QStringLiteral("day"), sample.day},
                                   {QStringLiteral("bytes"), double(sample.bytes)},
                                   {QStringLiteral("lines"), double(sample.lines)},
                                   {QStringLiteral("files"), double(sample.files)}});
    root.insert(QStringLiteral("days"), encoded);
    // Each new day lowers the ceiling to the repository's new size. It never
    // rises during that day while Ratchet Mode remains enabled.
    if (root.value(QStringLiteral("ratchet")).toBool()) {
        root.insert(QStringLiteral("ceilingDay"), current.day);
        root.insert(QStringLiteral("ceilingBytes"), double(current.bytes));
        root.insert(QStringLiteral("ceilingLines"), double(current.lines));
    }
    if (!writeDocument(statsPath(repoDir), root, error)) return {};
    return days;
}

bool RepoStatsStore::ratchetEnabled(const QString &repoDir)
{
    return readDocument(statsPath(repoDir)).value(QStringLiteral("ratchet")).toBool();
}

bool RepoStatsStore::setRatchetEnabled(const QString &repoDir, bool enabled,
                                       QString *error)
{
    QJsonObject root = readDocument(statsPath(repoDir));
    root.insert(QStringLiteral("ratchet"), enabled);
    if (enabled) {
        const RepoStatsSample now = measure(repoDir, error);
        if (now.day.isEmpty()) return false;
        root.insert(QStringLiteral("ceilingDay"), now.day);
        root.insert(QStringLiteral("ceilingBytes"), double(now.bytes));
        root.insert(QStringLiteral("ceilingLines"), double(now.lines));
    }
    return writeDocument(statsPath(repoDir), root, error);
}

bool RepoStatsStore::stagedCommitAllowed(const QString &repoDir, QString *reason)
{
    if (!ratchetEnabled(repoDir)) return true;
    bool ok = false;
    const QByteArray numstat = git(repoDir, {QStringLiteral("diff"),
                                            QStringLiteral("--cached"),
                                            QStringLiteral("--numstat")}, &ok);
    if (!ok) {
        if (reason) *reason = QStringLiteral("Ratchet could not inspect staged changes.");
        return false;
    }
    qint64 added = 0, removed = 0;
    for (const QByteArray &line : numstat.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 2 || fields.at(0) == "-" || fields.at(1) == "-") continue;
        added += fields.at(0).toLongLong();
        removed += fields.at(1).toLongLong();
    }
    if (added > removed) {
        if (reason) *reason = QStringLiteral("Ratchet Mode blocked this commit: %1 lines "
                                             "added but only %2 removed.")
                                 .arg(added).arg(removed);
        return false;
    }
    const QJsonObject root = readDocument(statsPath(repoDir));
    const qint64 ceiling = qint64(root.value(QStringLiteral("ceilingBytes")).toDouble());
    const RepoStatsSample now = measure(repoDir, nullptr);
    if (ceiling > 0 && now.bytes > ceiling) {
        if (reason) *reason = QStringLiteral("Ratchet Mode blocked this commit: tracked "
                                             "size is %1 bytes over today's ceiling.")
                                 .arg(now.bytes - ceiling);
        return false;
    }
    return true;
}

QString RepoStatsStore::agentGuidance(const QString &repoDir)
{
    if (!ratchetEnabled(repoDir)) return {};
    const QJsonObject root = readDocument(statsPath(repoDir));
    return QStringLiteral("Ratchet Mode is enabled for this repository. Do not add more "
                          "lines than you remove in any commit, and keep tracked size at "
                          "or below today's %1-byte ceiling. Refactor or delete existing "
                          "code before committing if necessary.")
        .arg(qint64(root.value(QStringLiteral("ceilingBytes")).toDouble()));
}
