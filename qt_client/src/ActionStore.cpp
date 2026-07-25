#include "ActionStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

namespace {

// Pre-v2 lossy key. Keep this implementation byte-for-byte compatible so old
// run directories and approval records can be migrated, but never use it for a
// new storage decision.
QString legacySanitize(const QString &owner, const QString &name)
{
    QString key = owner + QStringLiteral("-") + name;
    for (QChar &c : key)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('.'))
            c = QLatin1Char('_');
    return key;
}

QString canonicalRepositoryKey(const QString &owner, const QString &name)
{
    const QByteArray ownerBytes = owner.toUtf8();
    const QByteArray nameBytes = name.toUtf8();
    QByteArray material("forkmesh-actions-repository-v2\n");
    material.append(QByteArray::number(ownerBytes.size()));
    material.append(':');
    material.append(ownerBytes);
    material.append(QByteArray::number(nameBytes.size()));
    material.append(':');
    material.append(nameBytes);
    return QStringLiteral("v2-") +
           QString::fromLatin1(
               QCryptographicHash::hash(material, QCryptographicHash::Sha256)
                   .toHex());
}

bool runDirectoryMatches(const QString &directory, const ActionRun &run)
{
    QFile file(QDir(directory).filePath(QStringLiteral("meta.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    const ActionRun stored = ActionRun::fromJson(document.object());
    return stored.id == run.id && stored.owner == run.owner &&
           stored.name == run.name;
}

QString approvalKey(const QString &repoKey)
{
    return QStringLiteral("actions/approved/") + repoKey;
}

QByteArray reviewOnlyLegacyApprovals(const QByteArray &raw)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject())
        return {};
    QJsonObject approvals = document.object();
    for (auto it = approvals.begin(); it != approvals.end(); ++it) {
        if (!it.value().isObject())
            continue;
        QJsonObject approval = it.value().toObject();
        // A lossy key can never prove which repository was approved. Preserve
        // the prior YAML for a useful review diff, but invalidate its execution
        // authority and require a fresh repository-state-bound approval.
        approval.insert(QStringLiteral("schema"), 0);
        approval.insert(QStringLiteral("legacyScopeUnverified"), true);
        it.value() = approval;
    }
    return QJsonDocument(approvals).toJson(QJsonDocument::Compact);
}

} // namespace

QString ActionRun::repoKey() const
{
    return canonicalRepositoryKey(owner, name);
}

QString ActionRun::legacyRepoKey() const
{
    return legacySanitize(owner, name);
}

QJsonObject ActionRun::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["owner"] = owner;
    o["name"] = name;
    o["workflowPath"] = workflowPath;
    o["workflowName"] = workflowName;
    o["workflowContent"] = workflowContent;
    o["commit"] = commit;
    o["ref"] = ref;
    o["repositoryTree"] = repositoryTree;
    o["executionDigest"] = executionDigest;
    o["status"] = status;
    o["createdAtMs"] = createdAtMs;
    o["startedAtMs"] = startedAtMs;
    o["finishedAtMs"] = finishedAtMs;
    return o;
}

ActionRun ActionRun::fromJson(const QJsonObject &o)
{
    ActionRun r;
    r.id = o.value("id").toInt();
    r.owner = o.value("owner").toString();
    r.name = o.value("name").toString();
    r.workflowPath = o.value("workflowPath").toString();
    r.workflowName = o.value("workflowName").toString();
    r.workflowContent = o.value("workflowContent").toString();
    r.commit = o.value("commit").toString();
    r.ref = o.value("ref").toString();
    r.repositoryTree = o.value("repositoryTree").toString();
    r.executionDigest = o.value("executionDigest").toString();
    r.status = o.value("status").toString(ActionStatus::Queued);
    r.createdAtMs = o.value("createdAtMs").toVariant().toLongLong();
    r.startedAtMs = o.value("startedAtMs").toVariant().toLongLong();
    r.finishedAtMs = o.value("finishedAtMs").toVariant().toLongLong();
    return r;
}

ActionStore::ActionStore(QString rootDir) : m_root(std::move(rootDir))
{
    QDir().mkpath(runsDir());
    QDir().mkpath(spoolDir());
    migrateLegacyRecords();
}

QString ActionStore::runsDir() const { return m_root + QStringLiteral("/runs"); }
QString ActionStore::spoolDir() const { return m_root + QStringLiteral("/spool"); }
QString ActionStore::artifactsDir() const
{
    const QString path = m_root + QStringLiteral("/artifacts");
    QDir().mkpath(path);
    return path;
}

QString ActionStore::runDir(const ActionRun &run) const
{
    return runsDir() + QLatin1Char('/') + run.repoKey() + QLatin1Char('/') +
           QString::number(run.id);
}

QString ActionStore::legacyRunDir(const ActionRun &run) const
{
    return runsDir() + QLatin1Char('/') + run.legacyRepoKey() +
           QLatin1Char('/') + QString::number(run.id);
}

QString ActionStore::existingRunDir(const ActionRun &run) const
{
    const QString canonical = runDir(run);
    if (QFileInfo::exists(canonical))
        return canonical;
    const QString legacy = legacyRunDir(run);
    return runDirectoryMatches(legacy, run) ? legacy : canonical;
}

void ActionStore::migrateLegacyRecords()
{
    QDir root(runsDir());
    const QStringList repositoryDirectories =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QMap<QString, QSet<QString>> canonicalKeysByLegacyKey;

    for (const QString &repositoryDirectory : repositoryDirectories) {
        QDir repositoryDir(root.filePath(repositoryDirectory));
        const QStringList ids =
            repositoryDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids) {
            QFile meta(repositoryDir.filePath(id) +
                       QStringLiteral("/meta.json"));
            if (!meta.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument document =
                QJsonDocument::fromJson(meta.readAll());
            if (!document.isObject())
                continue;
            const ActionRun run = ActionRun::fromJson(document.object());
            if (run.id <= 0 || QString::number(run.id) != id ||
                run.owner.isEmpty() || run.name.isEmpty()) {
                continue;
            }

            canonicalKeysByLegacyKey[run.legacyRepoKey()].insert(
                run.repoKey());
            if (repositoryDirectory == run.repoKey())
                continue;

            const QString destinationParent =
                root.filePath(run.repoKey());
            const QString destination =
                QDir(destinationParent).filePath(id);
            if (!QFileInfo::exists(destination) &&
                QDir().mkpath(destinationParent)) {
                QDir().rename(repositoryDir.filePath(id), destination);
            }

            // Artifacts use the same repository/run hierarchy. Move the exact
            // run id alongside its metadata; a legacy collision can therefore
            // be split without granting either repository the other's files.
            const QString oldArtifact =
                QDir(m_root + QStringLiteral("/artifacts/") +
                     run.legacyRepoKey())
                    .filePath(id);
            const QString newArtifactParent =
                m_root + QStringLiteral("/artifacts/") + run.repoKey();
            const QString newArtifact =
                QDir(newArtifactParent).filePath(id);
            if (QFileInfo::exists(oldArtifact) &&
                !QFileInfo::exists(newArtifact) &&
                QDir().mkpath(newArtifactParent)) {
                QDir().rename(oldArtifact, newArtifact);
            }
        }
        if (repositoryDir.entryList(QDir::AllEntries |
                                    QDir::NoDotAndDotDot)
                .isEmpty()) {
            root.rmdir(repositoryDirectory);
        }
    }

    // A lossy legacy approval can be associated for review only when every
    // persisted run under that old key names one canonical repository. Even
    // then it is downgraded so it can show the previous YAML diff but cannot
    // execute or receive secrets without a fresh approval. If old records
    // prove a collision, leave the ambiguous record behind and fail closed.
    QSettings settings;
    for (auto it = canonicalKeysByLegacyKey.constBegin();
         it != canonicalKeysByLegacyKey.constEnd(); ++it) {
        if (it.value().size() != 1)
            continue;
        const QString oldKey = approvalKey(it.key());
        if (!settings.contains(oldKey))
            continue;
        const QByteArray raw = settings.value(oldKey).toByteArray();
        const QByteArray reviewOnly = reviewOnlyLegacyApprovals(raw);
        if (reviewOnly.isEmpty())
            continue;
        const QString newKey = approvalKey(*it.value().constBegin());
        if (!settings.contains(newKey))
            settings.setValue(newKey, reviewOnly);
        settings.sync();
        if (settings.status() == QSettings::NoError &&
            (settings.value(newKey).toByteArray() == reviewOnly ||
             settings.contains(newKey))) {
            settings.remove(oldKey);
            settings.sync();
        }
    }
}

int ActionStore::nextId() const
{
    int maxId = 0;
    QDir root(runsDir());
    const QStringList repos =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &repo : repos) {
        const QStringList ids = QDir(root.filePath(repo))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids)
            maxId = qMax(maxId, id.toInt());
    }
    return maxId + 1;
}

ActionRun ActionStore::createRun(ActionRun run)
{
    run.id = nextId();
    run.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(runDir(run));
    saveRun(run);
    return run;
}

bool ActionStore::saveRun(const ActionRun &run) const
{
    const QString directory = existingRunDir(run);
    QDir().mkpath(directory);
    QFile f(directory + QStringLiteral("/meta.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(run.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool ActionStore::deleteRun(const ActionRun &run) const
{
    bool removed = true;
    QDir canonical(runDir(run));
    if (canonical.exists())
        removed = canonical.removeRecursively();
    const QString legacyPath = legacyRunDir(run);
    if (runDirectoryMatches(legacyPath, run)) {
        QDir legacy(legacyPath);
        removed = legacy.removeRecursively() && removed;
    }
    return removed;
}

void ActionStore::appendLog(const ActionRun &run, const QString &text) const
{
    const QString directory = existingRunDir(run);
    QDir().mkpath(directory);
    QFile f(directory + QStringLiteral("/log.txt"));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    f.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        f.write("\n");
}

QString ActionStore::readLog(const ActionRun &run) const
{
    QFile f(existingRunDir(run) + QStringLiteral("/log.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QList<ActionRun> ActionStore::loadAllRuns() const
{
    QList<ActionRun> runs;
    QDir root(runsDir());
    const QStringList repos =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &repo : repos) {
        QDir repoDir(root.filePath(repo));
        const QStringList ids =
            repoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids) {
            QFile f(repoDir.filePath(id) + QStringLiteral("/meta.json"));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject())
                runs.append(ActionRun::fromJson(doc.object()));
        }
    }
    std::sort(runs.begin(), runs.end(),
              [](const ActionRun &a, const ActionRun &b) {
                  return a.createdAtMs > b.createdAtMs;
              });
    return runs;
}

// --- Variables --------------------------------------------------------------

QMap<QString, QString> ActionStore::variables()
{
    QMap<QString, QString> out;
    const QByteArray raw =
        QSettings().value(QStringLiteral("actions/variables")).toByteArray();
    const QJsonObject obj = QJsonDocument::fromJson(raw).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

void ActionStore::setVariables(const QMap<QString, QString> &vars)
{
    QJsonObject obj;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    QSettings().setValue(QStringLiteral("actions/variables"),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// --- Approvals --------------------------------------------------------------

bool ActionStore::isApproved(const QString &repoKey, const QString &path,
                             const QString &content,
                             const QString &repositoryTree,
                             const QString &executionDigest)
{
    if (repositoryTree.isEmpty() || executionDigest.isEmpty())
        return false;
    const QByteArray raw =
        QSettings().value(approvalKey(repoKey)).toByteArray();
    const QJsonValue stored =
        QJsonDocument::fromJson(raw).object().value(path);
    if (!stored.isObject())
        return false; // legacy YAML-only approvals deliberately fail closed
    const QJsonObject approval = stored.toObject();
    return approval.value(QStringLiteral("schema")).toInt() == 2 &&
           approval.value(QStringLiteral("workflowContent")).toString() == content &&
           approval.value(QStringLiteral("repositoryTree")).toString() ==
               repositoryTree &&
           approval.value(QStringLiteral("executionDigest")).toString() ==
               executionDigest;
}

void ActionStore::approve(const QString &repoKey, const QString &path,
                          const QString &content,
                          const QString &repositoryTree,
                          const QString &executionDigest)
{
    if (repositoryTree.isEmpty() || executionDigest.isEmpty())
        return; // an unbound approval is never persisted
    const QByteArray raw =
        QSettings().value(approvalKey(repoKey)).toByteArray();
    QJsonObject obj = QJsonDocument::fromJson(raw).object();
    obj.insert(path,
               QJsonObject{
                   {QStringLiteral("schema"), 2},
                   {QStringLiteral("workflowContent"), content},
                   {QStringLiteral("repositoryTree"), repositoryTree},
                   {QStringLiteral("executionDigest"), executionDigest},
               });
    QSettings().setValue(approvalKey(repoKey),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString ActionStore::lastApprovedContent(const QString &repoKey,
                                         const QString &path)
{
    const QByteArray raw =
        QSettings().value(approvalKey(repoKey)).toByteArray();
    const QJsonValue value =
        QJsonDocument::fromJson(raw).object().value(path);
    // Keep showing the previous YAML in the review diff while migrating a
    // legacy approval, but never treat that string-only entry as executable.
    return value.isObject()
               ? value.toObject()
                     .value(QStringLiteral("workflowContent"))
                     .toString()
               : value.toString();
}

bool ActionStore::repositoryStateDigest(const QString &repository,
                                        const QString &commit,
                                        QString *repositoryTree,
                                        QString *executionDigest,
                                        QString *error)
{
    if (repositoryTree)
        repositoryTree->clear();
    if (executionDigest)
        executionDigest->clear();
    if (error)
        error->clear();
    if (repository.trimmed().isEmpty() || commit.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("repository and commit are required");
        return false;
    }

    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    auto capture = [&](const QStringList &arguments, int timeoutMs,
                       QByteArray *output) {
        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QStringLiteral("git"), arguments);
        if (!process.waitForStarted(3000))
            return false;
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0)
            return false;
        *output = process.readAllStandardOutput();
        return true;
    };

    const QStringList base{QStringLiteral("-C"), repository};
    QByteArray treeBytes;
    if (!capture(base +
                     QStringList{QStringLiteral("rev-parse"),
                                 QStringLiteral("--verify"),
                                 commit + QStringLiteral("^{tree}")},
                 10000, &treeBytes)) {
        return fail(QStringLiteral("could not resolve the repository tree"));
    }
    const QString tree = QString::fromLatin1(treeBytes).trimmed().toLower();
    static const QRegularExpression objectId(
        QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
    if (!objectId.match(tree).hasMatch())
        return fail(QStringLiteral("repository returned an invalid tree id"));

    // Hash the raw recursive manifest first. This binds executable bits,
    // symlinks, paths, object ids, and submodule commit pins.
    QByteArray manifest;
    if (!capture(base +
                     QStringList{QStringLiteral("ls-tree"),
                                 QStringLiteral("-r"), QStringLiteral("-z"),
                                 QStringLiteral("--full-tree"), commit},
                 120000, &manifest)) {
        return fail(QStringLiteral("could not read the recursive repository tree"));
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(QByteArray("forkmesh-action-snapshot-v2\0", 28));
    digest.addData(tree.toLatin1());
    digest.addData(QByteArray("\0tree-manifest\0", 15));
    digest.addData(manifest);
    digest.addData(QByteArray("\0archive\0", 9));

    // Hash actual file bytes as well as Git object ids. Streaming keeps memory
    // bounded even for a large repository, while a hard deadline prevents a
    // corrupt or hostile repository from wedging workflow review.
    QProcess archive;
    archive.setProcessChannelMode(QProcess::SeparateChannels);
    archive.start(QStringLiteral("git"),
                  base +
                      QStringList{QStringLiteral("archive"),
                                  QStringLiteral("--format=tar"), commit});
    if (!archive.waitForStarted(3000))
        return fail(QStringLiteral("could not start repository snapshot hashing"));
    QElapsedTimer timer;
    timer.start();
    constexpr qint64 kDigestTimeoutMs = 5 * 60 * 1000;
    while (archive.state() != QProcess::NotRunning) {
        if (archive.waitForReadyRead(100))
            digest.addData(archive.readAllStandardOutput());
        if (timer.elapsed() > kDigestTimeoutMs) {
            archive.kill();
            archive.waitForFinished(1000);
            return fail(QStringLiteral("repository snapshot hashing timed out"));
        }
    }
    digest.addData(archive.readAllStandardOutput());
    if (archive.exitStatus() != QProcess::NormalExit ||
        archive.exitCode() != 0) {
        return fail(QStringLiteral("could not archive the repository snapshot"));
    }

    if (repositoryTree)
        *repositoryTree = tree;
    if (executionDigest)
        *executionDigest = QString::fromLatin1(digest.result().toHex());
    return true;
}
