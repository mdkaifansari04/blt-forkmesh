#include "RepoStatsStore.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>

namespace {
const int kHistoryDays = 30;
const char kStatsSkip[] = ".forkmesh/stats/";

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
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
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

// The directory shared by every worktree of a repository ("…/.git" for a normal
// clone). Linked worktrees each carry their own checkout of the tracked stats
// file, so a per-repository setting has to live here instead.
QString commonGitDir(const QString &repoDir)
{
    static QHash<QString, QString> cache;
    const auto cached = cache.constFind(repoDir);
    if (cached != cache.constEnd()) return *cached;
    bool ok = false;
    QString path = QString::fromUtf8(
                       git(repoDir, {QStringLiteral("rev-parse"),
                                     QStringLiteral("--git-common-dir")}, &ok))
                       .trimmed();
    if (!ok || path.isEmpty())
        path = QDir(repoDir).filePath(QStringLiteral(".git"));
    else if (QDir::isRelativePath(path))
        path = QDir(repoDir).filePath(path);
    path = QDir(path).absolutePath();
    cache.insert(repoDir, path);
    return path;
}

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes);
}

// Tracked bytes and file count of a committed tree, straight out of the object
// database — history without checking anything out.
RepoStatsSample treeSample(const QString &repoDir, const QString &sha)
{
    RepoStatsSample sample;
    bool ok = false;
    const QByteArray listing = git(repoDir, {QStringLiteral("ls-tree"),
                                             QStringLiteral("-r"),
                                             QStringLiteral("-l"), sha}, &ok);
    if (!ok) return sample;
    for (const QByteArray &line : listing.split('\n')) {
        const int tab = line.indexOf('\t');
        if (tab < 0 || line.mid(tab + 1).startsWith(kStatsSkip)) continue;
        const QList<QByteArray> fields = line.left(tab).simplified().split(' ');
        if (fields.size() < 4 || fields.at(1) != "blob") continue;
        sample.bytes += fields.at(3).toLongLong();
        ++sample.files;
    }
    return sample;
}
}

QString RepoStatsStore::statsPath(const QString &repoDir)
{
    const QDir gitDir(commonGitDir(repoDir));
    const QString root = gitDir.dirName() == QLatin1String(".git")
                             ? QFileInfo(gitDir.absolutePath()).absolutePath()
                             : gitDir.absolutePath();
    return QDir(root).filePath(QStringLiteral(".forkmesh/stats/repository.json"));
}

QString RepoStatsStore::statePath(const QString &repoDir)
{
    return QDir(commonGitDir(repoDir)).filePath(QStringLiteral("forkmesh-ratchet.json"));
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
        if (encoded.startsWith(kStatsSkip)) continue;
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

// Reconstructs the last 30 days from git history so a repository that never
// recorded a sample still opens with full charts: size and file counts from each
// day's last commit tree, lines walked back from today's measurement.
QVector<RepoStatsSample> RepoStatsStore::backfill(const QString &repoDir,
                                                  const RepoStatsSample &today)
{
    const QDate last = QDate::fromString(today.day, Qt::ISODate);
    const QDate first = last.addDays(-(kHistoryDays - 1));
    if (!last.isValid()) return {today};

    bool ok = false;
    QVector<QPair<QDate, QString>> commits; // newest first
    for (const QByteArray &line : git(repoDir, {QStringLiteral("log"),
                                                QStringLiteral("--first-parent"),
                                                QStringLiteral("--format=%H %cs")},
                                      &ok).split('\n')) {
        const int space = line.indexOf(' ');
        if (space <= 0) continue;
        const QDate when =
            QDate::fromString(QString::fromLatin1(line.mid(space + 1, 10)), Qt::ISODate);
        if (when.isValid())
            commits.append({when, QString::fromLatin1(line.left(space))});
    }
    if (commits.isEmpty()) return {today};

    QHash<QString, qint64> lineDelta;
    QString cursor;
    for (const QByteArray &line : git(repoDir, {QStringLiteral("log"),
                                                QStringLiteral("--first-parent"),
                                                QStringLiteral("-m"),
                                                QStringLiteral("--numstat"),
                                                QStringLiteral("--format=%x01%cs"),
                                                QStringLiteral("--since"),
                                                first.toString(Qt::ISODate)},
                                      &ok).split('\n')) {
        if (line.startsWith('\x01')) {
            cursor = QString::fromLatin1(line.mid(1, 10));
            continue;
        }
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 3 || fields.at(0) == "-") continue;
        if (fields.at(2).startsWith(kStatsSkip)) continue;
        lineDelta[cursor] += fields.at(0).toLongLong() - fields.at(1).toLongLong();
    }

    QHash<QString, RepoStatsSample> treeCache;
    QVector<RepoStatsSample> days;
    qint64 lines = today.lines;
    for (QDate day = last; day >= first; day = day.addDays(-1)) {
        const QString key = day.toString(Qt::ISODate);
        if (key == today.day) {
            days.prepend(today);
        } else {
            QString sha;
            for (const QPair<QDate, QString> &commit : commits) {
                if (commit.first <= day) { sha = commit.second; break; }
            }
            if (sha.isEmpty()) break; // before the repository's first commit
            if (!treeCache.contains(sha)) treeCache.insert(sha, treeSample(repoDir, sha));
            RepoStatsSample sample = treeCache.value(sha);
            sample.day = key;
            sample.lines = qMax<qint64>(0, lines);
            days.prepend(sample);
        }
        lines -= lineDelta.value(key);
    }
    return days;
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
    if (result.size() > kHistoryDays) result = result.mid(result.size() - kHistoryDays);
    Q_UNUSED(error);
    return result;
}

QVector<RepoStatsSample> RepoStatsStore::captureDaily(const QString &repoDir,
                                                       QString *error)
{
    QVector<RepoStatsSample> days = load(repoDir, error);
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (!days.isEmpty() && days.last().day == today) {
        // Already sampled today: keep the ceiling pinned to that first reading.
        if (ratchetEnabled(repoDir)) ceilingBytes(repoDir, days.last(), true);
        return days;
    }
    const RepoStatsSample current = measure(repoDir, error);
    if (current.day.isEmpty()) return days;
    if (days.isEmpty()) days = backfill(repoDir, current);
    else days.append(current);
    while (days.size() > kHistoryDays) days.removeFirst();
    QJsonObject root = readDocument(statsPath(repoDir));
    QJsonArray encoded;
    for (const RepoStatsSample &sample : days)
        encoded.append(QJsonObject{{QStringLiteral("day"), sample.day},
                                   {QStringLiteral("bytes"), double(sample.bytes)},
                                   {QStringLiteral("lines"), double(sample.lines)},
                                   {QStringLiteral("files"), double(sample.files)}});
    root.insert(QStringLiteral("days"), encoded);
    // Each new day re-bases the ceiling on the repository's size right now. It
    // never rises again during that day while Ratchet Mode remains enabled.
    if (ratchetEnabled(repoDir)) ceilingBytes(repoDir, current, true);
    if (!writeDocument(statsPath(repoDir), root, error)) return {};
    return days;
}

bool RepoStatsStore::ratchetEnabled(const QString &repoDir)
{
    return readDocument(statePath(repoDir)).value(QStringLiteral("ratchet")).toBool();
}

bool RepoStatsStore::setRatchetEnabled(const QString &repoDir, bool enabled,
                                       QString *error)
{
    QJsonObject state = readDocument(statePath(repoDir));
    state.insert(QStringLiteral("ratchet"), enabled);
    // Turning it off clears the ceiling: nothing is left that a later toggle,
    // checkout or merge could read back as an active limit.
    state.remove(QStringLiteral("ceilingDay"));
    state.remove(QStringLiteral("ceilingBytes"));
    if (enabled) {
        const RepoStatsSample now = measure(repoDir, error);
        if (now.day.isEmpty()) return false;
        state.insert(QStringLiteral("ceilingDay"), now.day);
        state.insert(QStringLiteral("ceilingBytes"), double(now.bytes));
    }
    return writeDocument(statePath(repoDir), state, error);
}

// Today's byte ceiling. A ceiling left over from an earlier day is stale — the
// ratchet resets every day — so it is re-based on the current size on request.
qint64 RepoStatsStore::ceilingBytes(const QString &repoDir, const RepoStatsSample &now,
                                    bool refreshStaleDay)
{
    QJsonObject state = readDocument(statePath(repoDir));
    const qint64 stored = qint64(state.value(QStringLiteral("ceilingBytes")).toDouble());
    if (state.value(QStringLiteral("ceilingDay")).toString() == now.day && stored > 0)
        return stored;
    if (!refreshStaleDay) return stored;
    state.insert(QStringLiteral("ceilingDay"), now.day);
    state.insert(QStringLiteral("ceilingBytes"), double(now.bytes));
    writeDocument(statePath(repoDir), state, nullptr);
    return now.bytes;
}

bool RepoStatsStore::stagedCommitAllowed(const QString &repoDir, QString *reason)
{
    if (!ratchetEnabled(repoDir)) return true;
    const RepoStatsSample now = measure(repoDir, nullptr);
    if (now.day.isEmpty()) {
        if (reason) *reason = QStringLiteral("Ratchet could not measure the repository.");
        return false;
    }
    const qint64 ceiling = ceilingBytes(repoDir, now, true);
    if (ceiling > 0 && now.bytes > ceiling) {
        if (reason)
            *reason = QStringLiteral("Ratchet Mode blocked this commit: it would leave the "
                                     "repository at %1, which is %2 over today's ceiling of "
                                     "%3. Delete or shrink tracked files first.")
                          .arg(humanSize(now.bytes), humanSize(now.bytes - ceiling),
                               humanSize(ceiling));
        return false;
    }
    return true;
}

QString RepoStatsStore::agentGuidance(const QString &repoDir)
{
    if (!ratchetEnabled(repoDir)) return {};
    const RepoStatsSample now = measure(repoDir, nullptr);
    const qint64 ceiling = ceilingBytes(repoDir, now, false);
    if (ceiling <= 0) return {};
    return QStringLiteral("Ratchet Mode is enabled for this repository and measures size, "
                          "not line count. Tracked files total %1 against today's ceiling "
                          "of %2 (%3 of headroom); commits that go over are rejected, so "
                          "delete dead code or unused assets to pay for anything you add.")
        .arg(humanSize(now.bytes), humanSize(ceiling),
             humanSize(qMax<qint64>(0, ceiling - now.bytes)));
}
