#include "AgentStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace {

QString sanitize(const QString &owner, const QString &name)
{
    QString key = owner + QStringLiteral("-") + name;
    for (QChar &c : key)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('.'))
            c = QLatin1Char('_');
    return key;
}

} // namespace

QString AgentSession::repoKey() const
{
    return sanitize(owner, name);
}

QJsonObject AgentSession::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["owner"] = owner;
    obj["name"] = name;
    obj["issueNumber"] = issueNumber;
    obj["issueTitle"] = issueTitle;
    obj["prompt"] = prompt;
    obj["provider"] = provider;
    obj["model"] = model;
    obj["createPr"] = createPr;
    obj["prNumber"] = prNumber;
    obj["status"] = status;
    obj["branchName"] = branchName;
    obj["baseRef"] = baseRef;
    obj["baseBranch"] = baseBranch;
    obj["merged"] = merged;
    obj["mergedAtMs"] = mergedAtMs;
    obj["createdAtMs"] = createdAtMs;
    obj["startedAtMs"] = startedAtMs;
    obj["finishedAtMs"] = finishedAtMs;
    obj["promptTokens"] = promptTokens;
    obj["completionTokens"] = completionTokens;
    obj["totalTokens"] = totalTokens;
    obj["contextTokens"] = contextTokens;
    obj["contextWindow"] = contextWindow;
    obj["maxOutputTokens"] = maxOutputTokens;
    obj["estimatedCredits"] = estimatedCredits;
    obj["costUsd"] = costUsd;
    obj["spendBeforeUsd"] = spendBeforeUsd;
    obj["spendAfterUsd"] = spendAfterUsd;
    obj["numTurns"] = numTurns;
    obj["durationMs"] = durationMs;
    obj["lastError"] = lastError;
    return obj;
}

AgentSession AgentSession::fromJson(const QJsonObject &obj)
{
    AgentSession session;
    session.id = obj.value("id").toInt();
    session.owner = obj.value("owner").toString();
    session.name = obj.value("name").toString();
    session.issueNumber = obj.value("issueNumber").toInt();
    session.issueTitle = obj.value("issueTitle").toString();
    session.prompt = obj.value("prompt").toString();
    session.provider = obj.value("provider").toString();
    session.model = obj.value("model").toString();
    session.createPr = obj.value("createPr").toBool();
    session.prNumber = obj.value("prNumber").toInt();
    session.status = obj.value("status").toString(AgentStatus::Queued);
    session.branchName = obj.value("branchName").toString();
    session.baseRef = obj.value("baseRef").toString();
    session.baseBranch = obj.value("baseBranch").toString();
    session.merged = obj.value("merged").toBool();
    session.mergedAtMs = obj.value("mergedAtMs").toVariant().toLongLong();
    session.createdAtMs = obj.value("createdAtMs").toVariant().toLongLong();
    session.startedAtMs = obj.value("startedAtMs").toVariant().toLongLong();
    session.finishedAtMs = obj.value("finishedAtMs").toVariant().toLongLong();
    session.promptTokens = obj.value("promptTokens").toInt();
    session.completionTokens = obj.value("completionTokens").toInt();
    session.totalTokens = obj.value("totalTokens").toInt();
    session.contextTokens = obj.value("contextTokens").toInt();
    session.contextWindow = obj.value("contextWindow").toInt();
    session.maxOutputTokens = obj.value("maxOutputTokens").toInt();
    session.estimatedCredits = obj.value("estimatedCredits").toInt();
    session.costUsd = obj.value("costUsd").toDouble();
    session.spendBeforeUsd = obj.value("spendBeforeUsd").toDouble();
    session.spendAfterUsd = obj.value("spendAfterUsd").toDouble();
    session.numTurns = obj.value("numTurns").toInt();
    session.durationMs = obj.value("durationMs").toVariant().toLongLong();
    session.lastError = obj.value("lastError").toString();
    return session;
}

AgentStore::AgentStore(QString rootDir) : m_root(std::move(rootDir))
{
    QDir().mkpath(sessionsDir());
}

QString AgentStore::sessionsDir() const
{
    return m_root + QStringLiteral("/sessions");
}

QString AgentStore::sessionDir(const AgentSession &session) const
{
    return sessionsDir() + QLatin1Char('/') + session.repoKey() +
           QLatin1Char('/') + QString::number(session.id);
}

int AgentStore::nextId() const
{
    int maxId = 0;
    QDir root(sessionsDir());
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

AgentSession AgentStore::createSession(AgentSession session)
{
    session.id = nextId();
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(sessionDir(session));
    saveSession(session);
    return session;
}

bool AgentStore::saveSession(const AgentSession &session) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/meta.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(session.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool AgentStore::deleteSession(const AgentSession &session) const
{
    QDir dir(sessionDir(session));
    if (!dir.exists())
        return true;
    return dir.removeRecursively();
}

void AgentStore::appendLog(const AgentSession &session, const QString &text) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/transcript.txt"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        file.write("\n");
}

QString AgentStore::readLog(const AgentSession &session) const
{
    QFile file(sessionDir(session) + QStringLiteral("/transcript.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void AgentStore::appendEvent(const AgentSession &session, const QJsonObject &ev) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/events.jsonl"));
    if (!file.open(QIODevice::Append))
        return;
    file.write(QJsonDocument(ev).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QList<QJsonObject> AgentStore::loadEvents(const AgentSession &session) const
{
    QList<QJsonObject> events;
    QFile file(sessionDir(session) + QStringLiteral("/events.jsonl"));
    if (!file.open(QIODevice::ReadOnly))
        return events;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            events.append(doc.object());
    }
    return events;
}

void AgentStore::clearEvents(const AgentSession &session) const
{
    QFile::remove(sessionDir(session) + QStringLiteral("/events.jsonl"));
}

void AgentStore::writePatch(const AgentSession &session, const QString &patch) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/changes.patch"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    file.write(patch.toUtf8());
}

QString AgentStore::readPatch(const AgentSession &session) const
{
    QFile file(sessionDir(session) + QStringLiteral("/changes.patch"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QList<AgentSession> AgentStore::loadAllSessions() const
{
    QList<AgentSession> sessions;
    QDir root(sessionsDir());
    const QStringList repos =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &repo : repos) {
        QDir repoDir(root.filePath(repo));
        const QStringList ids =
            repoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids) {
            QFile file(repoDir.filePath(id) + QStringLiteral("/meta.json"));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject())
                sessions.append(AgentSession::fromJson(doc.object()));
        }
    }
    std::sort(sessions.begin(), sessions.end(),
              [](const AgentSession &a, const AgentSession &b) {
                  return a.createdAtMs > b.createdAtMs;
              });
    return sessions;
}
