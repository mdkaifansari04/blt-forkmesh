#include "RepoStatsStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

static bool run(const QString &dir, const QStringList &args)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), args);
    return p.waitForFinished() && p.exitCode() == 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid() || !run(tmp.path(), {QStringLiteral("init"),
                                            QStringLiteral("-q")})) return 1;
    run(tmp.path(), {QStringLiteral("config"), QStringLiteral("user.email"),
                     QStringLiteral("test@example.invalid")});
    run(tmp.path(), {QStringLiteral("config"), QStringLiteral("user.name"),
                     QStringLiteral("Test")});
    QFile source(QDir(tmp.path()).filePath(QStringLiteral("source.cpp")));
    source.open(QIODevice::WriteOnly);
    source.write("one\ntwo\n");
    source.close();
    if (!run(tmp.path(), {QStringLiteral("add"), QStringLiteral("source.cpp")}) ||
        !run(tmp.path(), {QStringLiteral("commit"), QStringLiteral("-qm"),
                          QStringLiteral("seed")})) return 1;

    QString error;
    const QVector<RepoStatsSample> first = RepoStatsStore::captureDaily(tmp.path(), &error);
    if (first.size() != 1 || first.last().files != 1 || first.last().lines != 2 ||
        !QFileInfo::exists(QDir(tmp.path()).filePath(
            QStringLiteral(".forkmesh/stats/repository.json")))) return 1;
    QFile stats(QDir(tmp.path()).filePath(
        QStringLiteral(".forkmesh/stats/repository.json")));
    if (!stats.open(QIODevice::ReadOnly)) return 1;
    QJsonObject legacyStats = QJsonDocument::fromJson(stats.readAll()).object();
    stats.close();
    legacyStats.insert(QStringLiteral("ratchet"), true);
    if (!stats.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    stats.write(QJsonDocument(legacyStats).toJson(QJsonDocument::Indented));
    stats.close();
    if (!RepoStatsStore::ratchetEnabled(tmp.path()) ||
        !stats.open(QIODevice::ReadOnly)) return 1;
    const QByteArray statsBeforeToggle = stats.readAll();
    stats.close();
    if (!RepoStatsStore::setRatchetEnabled(tmp.path(), true, &error) ||
        !RepoStatsStore::ratchetEnabled(tmp.path()) ||
        RepoStatsStore::agentGuidance(tmp.path()).isEmpty()) return 1;
    if (!stats.open(QIODevice::ReadOnly) || stats.readAll() != statsBeforeToggle)
        return 1;
    stats.close();

    source.open(QIODevice::Append);
    source.write("three\n");
    source.close();
    run(tmp.path(), {QStringLiteral("add"), QStringLiteral("source.cpp")});
    QString reason;
    if (RepoStatsStore::stagedCommitAllowed(tmp.path(), &reason) ||
        !reason.contains(QStringLiteral("added"))) return 1;

    // Disabling is immediate and local. It overrides any legacy committed
    // state and must not create a repository change that itself needs a commit.
    if (!RepoStatsStore::setRatchetEnabled(tmp.path(), false, &error) ||
        RepoStatsStore::ratchetEnabled(tmp.path()) ||
        !RepoStatsStore::agentGuidance(tmp.path()).isEmpty() ||
        !RepoStatsStore::stagedCommitAllowed(tmp.path(), &reason)) return 1;
    if (!stats.open(QIODevice::ReadOnly) || stats.readAll() != statsBeforeToggle)
        return 1;
    stats.close();

    if (!RepoStatsStore::setRatchetEnabled(tmp.path(), true, &error)) return 1;

    source.open(QIODevice::WriteOnly | QIODevice::Truncate);
    source.write("one\n");
    source.close();
    run(tmp.path(), {QStringLiteral("add"), QStringLiteral("source.cpp")});
    if (!RepoStatsStore::stagedCommitAllowed(tmp.path(), &reason)) return 1;
    std::cout << "repo stats tests passed\n";
    return 0;
}
