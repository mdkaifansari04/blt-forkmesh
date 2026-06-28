#include "ActionStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

// Sanitize an owner/name pair into a filesystem- and settings-safe key.
QString sanitize(const QString &owner, const QString &name)
{
    QString key = owner + QStringLiteral("-") + name;
    for (QChar &c : key)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('.'))
            c = QLatin1Char('_');
    return key;
}

} // namespace

QString ActionRun::repoKey() const
{
    return sanitize(owner, name);
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
}

QString ActionStore::runsDir() const { return m_root + QStringLiteral("/runs"); }
QString ActionStore::spoolDir() const { return m_root + QStringLiteral("/spool"); }

QString ActionStore::runDir(const ActionRun &run) const
{
    return runsDir() + QLatin1Char('/') + run.repoKey() + QLatin1Char('/') +
           QString::number(run.id);
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
    QDir().mkpath(runDir(run));
    QFile f(runDir(run) + QStringLiteral("/meta.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(run.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool ActionStore::deleteRun(const ActionRun &run) const
{
    QDir dir(runDir(run));
    if (!dir.exists())
        return true;
    return dir.removeRecursively();
}

void ActionStore::appendLog(const ActionRun &run, const QString &text) const
{
    QDir().mkpath(runDir(run));
    QFile f(runDir(run) + QStringLiteral("/log.txt"));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    f.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        f.write("\n");
}

QString ActionStore::readLog(const ActionRun &run) const
{
    QFile f(runDir(run) + QStringLiteral("/log.txt"));
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

static QString approvalKey(const QString &repoKey)
{
    return QStringLiteral("actions/approved/") + repoKey;
}

bool ActionStore::isApproved(const QString &repoKey, const QString &path,
                             const QString &content)
{
    return lastApprovedContent(repoKey, path) == content;
}

void ActionStore::approve(const QString &repoKey, const QString &path,
                          const QString &content)
{
    const QByteArray raw =
        QSettings().value(approvalKey(repoKey)).toByteArray();
    QJsonObject obj = QJsonDocument::fromJson(raw).object();
    obj.insert(path, content);
    QSettings().setValue(approvalKey(repoKey),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString ActionStore::lastApprovedContent(const QString &repoKey,
                                         const QString &path)
{
    const QByteArray raw =
        QSettings().value(approvalKey(repoKey)).toByteArray();
    return QJsonDocument::fromJson(raw).object().value(path).toString();
}
